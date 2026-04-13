#include "../../../include/druid.h"
#include <stdlib.h>
#include <string.h>
#include <omp.h>

// Forward declarations for static helpers
static void frustumExtract(const Mat4 *vp, Frustum *out);
static b8   frustumTestSphere(const Frustum *f, f32 cx, f32 cy, f32 cz, f32 r);

//=====================================================================================================================
// Renderer — global rendering context
//
// Uses the generic Buffer struct for cameras, instance buffers and GBuffers.
// Acquire a slot, use it, release when done.
//=====================================================================================================================

Renderer *renderer = NULL;

Renderer *createRenderer(Display *display, f32 fov, f32 nearClip, f32 farClip,
                         u32 maxCameras, u32 maxIBuffers, u32 maxGBuffers)
{
    if (!display)
    {
        ERROR("createRenderer: NULL display");
        return NULL;
    }
    if (maxCameras == 0)  maxCameras  = 8;
    if (maxIBuffers == 0) maxIBuffers = 8;
    if (maxGBuffers == 0) maxGBuffers = 4;

    Renderer *r = (Renderer *)dalloc(sizeof(Renderer), MEM_TAG_RENDERER);
    if (!r)
    {
        ERROR("createRenderer: malloc failed");
        return NULL;
    }
    memset(r, 0, sizeof(Renderer));

    r->display = display;

    // create buffers
    if (!bufferCreate(&r->cameras, sizeof(Camera), maxCameras) ||
        !bufferCreate(&r->instanceBuffers, sizeof(InstanceBuffer), maxIBuffers) ||
        !bufferCreate(&r->gBuffers, sizeof(GBuffer), maxGBuffers))
    {
        ERROR("createRenderer: buffer creation failed");
        bufferDestroy(&r->cameras);
        bufferDestroy(&r->instanceBuffers);
        bufferDestroy(&r->gBuffers);
        dfree(r, sizeof(Renderer), MEM_TAG_RENDERER);
        return NULL;
    }

    // core UBO shared by all shaders
    r->coreUBO = createCoreShaderUBO();

    r->time = 0.0f;
    r->defaultShader = 0;

    // model-matrix SSBO (binding point 2) — capacity matches the default instance buffer
    r->modelSSBO = modelSSBOCreate(RENDERER_MAX_INSTANCE);
    if (!r->modelSSBO)
    {
        WARN("createRenderer: modelSSBOCreate failed — per-draw SSBO disabled");
    }

    // indirect rendering buffer (bindles multiple glDrawElementsIndirect commands)
    r->indirectBuffer = indirectBufferCreate(256);  // max 256 unique draw batches
    if (!r->indirectBuffer)
    {
        WARN("createRenderer: indirectBufferCreate failed — indirect rendering disabled");
    }

    r->activeGBuffer = (u32)-1;
    r->useDeferredRendering = false;
    r->envMapTex = 0;

    r->activeCamera = (u32)-1; // no default camera — caller must acquire one
    r->defaultIBuffer = (u32)-1;

    // acquire a default instance buffer
    // Try full capacity first; if GPU can't allocate 128MB persistent-mapped SSBO,
    // fall back to 256K (16MB), then 64K (4MB).
    {
        static const u32 trySizes[] = { RENDERER_MAX_INSTANCE, 262144, 65536 };
        u32 ibSlot = (u32)-1;
        for (u32 t = 0; t < 3 && ibSlot == (u32)-1; t++)
        {
            ibSlot = rendererAcquireInstanceBuffer(r, trySizes[t]);
            if (ibSlot != (u32)-1)
            {
                InstanceBuffer *ib = (InstanceBuffer *)bufferGet(&r->instanceBuffers, ibSlot);
                if (!ib || !ib->ready)
                {
                    WARN("createRenderer: instance buffer %uK NOT ready — retrying smaller", trySizes[t] / 1024);
                    rendererReleaseInstanceBuffer(r, ibSlot);
                    ibSlot = (u32)-1;
                }
                else
                {
                    INFO("createRenderer: instance buffer slot %u ready, capacity %u (%uMB SSBO)",
                         ibSlot, ib->capacity, (u32)(ib->capacity * sizeof(Mat4) * 2 / (1024 * 1024)));
                }
            }
        }
        if (ibSlot == (u32)-1)
            WARN("createRenderer: ALL instance buffer sizes failed — instanced rendering disabled");
        r->defaultIBuffer = ibSlot;
    }

    // Enable deferred rendering by default
    u32 gbW = (u32)display->screenWidth;
    u32 gbH = (u32)display->screenHeight;
    if (gbW > 0 && gbH > 0)
    {
        rendererEnableDeferred(r, gbW, gbH);
    }

    renderer = r;

    INFO("Renderer created (%gx%g, cams=%u, ibufs=%u, gbufs=%u, deferred=%s, indirect=%s)",
         display->screenWidth, display->screenHeight,
         maxCameras, maxIBuffers, maxGBuffers,
         r->useDeferredRendering ? "on" : "off",
         r->indirectBuffer ? "on" : "off");
    return r;
}

static u32 s_lastBoundVAO = 0;

void rendererBeginFrame(Renderer *r, f32 dt)
{
    if (!r) return;
    s_lastBoundVAO = 0;

    r->time += dt;
    r->frameVisible = NULL;
    r->frameVisibleCount = 0;

    // push active camera + timing into the core UBO so every shader sees them
    // Also extract frustum planes ONCE per frame (cached in Renderer)
    r->hasFrustum = false;
    u32 ci = r->activeCamera;
    Camera *cam = (Camera *)bufferGet(&r->cameras, ci);
    if (cam && bufferIsOccupied(&r->cameras, ci))
    {
        Mat4 view = getView(cam, false);
        updateCoreShaderUBO(r->time, &cam->pos, &view, &cam->projection);

        Mat4 vp = getViewProjection(cam);
        frustumExtract(&vp, &r->frustum);
        r->hasFrustum = true;
    }

    // Advance all double-buffered instance SSBOs: rotate write index, wait on fence
    for (u32 i = 0; i < r->instanceBuffers.capacity; i++)
    {
        if (bufferIsOccupied(&r->instanceBuffers, i))
        {
            InstanceBuffer *buf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, i);
            instanceBufferAdvance(buf);
        }
    }

    // reset model SSBO write cursor and bind it to slot 2 for this frame
    if (r->modelSSBO)
    {
        modelSSBOBeginFrame(r->modelSSBO);
        modelSSBOBind(r->modelSSBO, 2);
    }

    // reset indirect buffer for this frame
    if (r->indirectBuffer)
    {
        indirectBufferReset(r->indirectBuffer);
    }
}

void rendererFlushInstances(Renderer *r, u32 ibufferIndex, u32 shaderProgram)
{
    if (!r) return;
    if (!bufferIsOccupied(&r->instanceBuffers, ibufferIndex)) return;

    InstanceBuffer *buf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, ibufferIndex);
    if (!buf || !buf->ready || buf->count == 0) return;

    // bind the current write buffer's SSBO to binding point 1
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buf->buffer[buf->writeIdx]);
    glUseProgram(shaderProgram);
}

void destroyRenderer(Renderer *r)
{
    if (!r) return;

    // destroy GPU resources held by occupied instance buffers
    for (u32 i = 0; i < r->instanceBuffers.capacity; i++)
    {
        if (bufferIsOccupied(&r->instanceBuffers, i))
        {
            InstanceBuffer *buf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, i);
            instanceBufferDestroy(buf);
        }
    }

    // destroy GPU resources held by occupied GBuffers
    for (u32 i = 0; i < r->gBuffers.capacity; i++)
    {
        if (bufferIsOccupied(&r->gBuffers, i))
        {
            GBuffer *gb = (GBuffer *)bufferGet(&r->gBuffers, i);
            glDeleteFramebuffers(1, &gb->fbo);
            glDeleteTextures(1, &gb->positionTex);
            glDeleteTextures(1, &gb->normalTex);
            glDeleteTextures(1, &gb->albedoSpecTex);
            glDeleteTextures(1, &gb->depthTex);
        }
    }

    r->activeGBuffer = (u32)-1;

    // free the buffers themselves
    bufferDestroy(&r->cameras);
    bufferDestroy(&r->instanceBuffers);
    bufferDestroy(&r->gBuffers);

    if (r->coreUBO)
        freeUBO(r->coreUBO);

    if (r->modelSSBO)
        modelSSBODestroy(r->modelSSBO);

    if (r->indirectBuffer)
        indirectBufferDestroy(r->indirectBuffer);

    if (renderer == r)
        renderer = NULL;

    dfree(r, sizeof(Renderer), MEM_TAG_RENDERER);
    INFO("Renderer destroyed");
}

//=====================================================================================================================
// Convenience acquire / release wrappers
//=====================================================================================================================

u32 rendererAcquireCamera(Renderer *r, Vec3 pos, f32 fov, f32 aspect,
                          f32 nearClip, f32 farClip)
{
    if (!r) return (u32)-1;

    u32 i = bufferAcquire(&r->cameras);
    if (i == (u32)-1) return i;

    Camera *cam = (Camera *)bufferGet(&r->cameras, i);
    initCamera(cam, pos, fov, aspect, nearClip, farClip);

    DEBUG("Renderer: acquired camera slot %u", i);
    return i;
}

void rendererReleaseCamera(Renderer *r, u32 index)
{
    if (!r || !bufferIsOccupied(&r->cameras, index)) return;

    bufferRelease(&r->cameras, index);

    // if we just released the active camera, fall back to slot 0
    if (r->activeCamera == index)
        r->activeCamera = 0;

    DEBUG("Renderer: released camera slot %u", index);
}

u32 rendererAcquireInstanceBuffer(Renderer *r, u32 capacity)
{
    if (!r) return (u32)-1;

    u32 i = bufferAcquire(&r->instanceBuffers);
    if (i == (u32)-1) return i;

    InstanceBuffer *buf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, i);
    instanceBufferCreate(buf, capacity);

    DEBUG("Renderer: acquired instance buffer slot %u (cap %u)", i, capacity);
    return i;
}

void rendererReleaseInstanceBuffer(Renderer *r, u32 index)
{
    if (!r || !bufferIsOccupied(&r->instanceBuffers, index)) return;

    InstanceBuffer *buf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, index);
    instanceBufferDestroy(buf);
    bufferRelease(&r->instanceBuffers, index);

    DEBUG("Renderer: released instance buffer slot %u", index);
}

u32 rendererAcquireGBuffer(Renderer *r, u32 width, u32 height)
{
    if (!r) return (u32)-1;

    u32 i = bufferAcquire(&r->gBuffers);
    if (i == (u32)-1) return i;

    GBuffer *gb = (GBuffer *)bufferGet(&r->gBuffers, i);
    *gb = createGBuffer(width, height);

    DEBUG("Renderer: acquired GBuffer slot %u (%ux%u)", i, width, height);
    return i;
}

void rendererReleaseGBuffer(Renderer *r, u32 index)
{
    if (!r || !bufferIsOccupied(&r->gBuffers, index)) return;

    GBuffer *gb = (GBuffer *)bufferGet(&r->gBuffers, index);
    glDeleteFramebuffers(1, &gb->fbo);
    glDeleteTextures(1, &gb->positionTex);
    glDeleteTextures(1, &gb->normalTex);
    glDeleteTextures(1, &gb->albedoSpecTex);
    glDeleteTextures(1, &gb->depthTex);

    bufferRelease(&r->gBuffers, index);

    DEBUG("Renderer: released GBuffer slot %u", index);
}

//=====================================================================================================================
// Deferred rendering pipeline
//=====================================================================================================================

DAPI b8 rendererEnableDeferred(Renderer *r, u32 width, u32 height)
{
    if (!r) return false;

    // Release old GBuffer if one exists
    if (r->activeGBuffer != (u32)-1) {
        rendererReleaseGBuffer(r, r->activeGBuffer);
    }

    u32 slot = rendererAcquireGBuffer(r, width, height);
    if (slot == (u32)-1) {
        ERROR("rendererEnableDeferred: failed to acquire GBuffer");
        return false;
    }

    r->activeGBuffer = slot;
    r->useDeferredRendering = true;
    INFO("Deferred rendering enabled (%ux%u)", width, height);
    return true;
}

DAPI void rendererDisableDeferred(Renderer *r)
{
    if (!r) return;

    if (r->activeGBuffer != (u32)-1) {
        rendererReleaseGBuffer(r, r->activeGBuffer);
        r->activeGBuffer = (u32)-1;
    }
    r->useDeferredRendering = false;
    INFO("Deferred rendering disabled");
}

DAPI void rendererBeginDeferredPass(Renderer *r)
{
    if (!r || r->activeGBuffer == (u32)-1) return;

    GBuffer *gb = (GBuffer *)bufferGet(&r->gBuffers, r->activeGBuffer);
    if (!gb) return;

    glBindFramebuffer(GL_FRAMEBUFFER, gb->fbo);

    GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glDrawBuffers(3, attachments);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

DAPI void rendererEndDeferredPass(Renderer *r)
{
    if (!r) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

DAPI void rendererLightingPass(Renderer *r, u32 lightingShader)
{
    if (!r || r->activeGBuffer == (u32)-1 || lightingShader == 0) return;

    GBuffer *gb = (GBuffer *)bufferGet(&r->gBuffers, r->activeGBuffer);
    if (!gb) return;

    glUseProgram(lightingShader);

    // Cache uniform locations — only query GL when shader changes
    static u32 s_cachedLightShader = 0;
    static i32 s_locGPos = -1, s_locGNorm = -1, s_locGAlbedo = -1, s_locEnvMap = -1;
    if (lightingShader != s_cachedLightShader)
    {
        s_cachedLightShader = lightingShader;
        s_locGPos    = glGetUniformLocation(lightingShader, "gPosition");
        s_locGNorm   = glGetUniformLocation(lightingShader, "gNormal");
        s_locGAlbedo = glGetUniformLocation(lightingShader, "gAlbedoSpec");
        s_locEnvMap  = glGetUniformLocation(lightingShader, "envMap");
    }

    // Bind GBuffer textures to sequential texture units
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gb->positionTex);
    glUniform1i(s_locGPos, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gb->normalTex);
    glUniform1i(s_locGNorm, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gb->albedoSpecTex);
    glUniform1i(s_locGAlbedo, 2);

    // Bind environment cubemap for IBL reflections (unit 3)
    glActiveTexture(GL_TEXTURE3);
    if (r->envMapTex != 0)
        glBindTexture(GL_TEXTURE_CUBE_MAP, r->envMapTex);
    else
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glUniform1i(s_locEnvMap, 3);

    // Disable depth writes so the fullscreen quad doesn't pollute the depth buffer
    glDisable(GL_DEPTH_TEST);

    // Render full-screen quad
    static u32 quadVAO = 0, quadVBO = 0;
    if (quadVAO == 0) {
        f32 quadVertices[] = {
            // positions   // texCoords
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,

            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f,
        };
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), (void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(f32), (void *)(2 * sizeof(f32)));
        glBindVertexArray(0);
    }

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);

    // Blit GBuffer depth into the current framebuffer so subsequent forward
    // passes (skybox, gizmos) have correct depth information.
    {
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, gb->fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFBO);
        glBlitFramebuffer(0, 0, (GLint)gb->width, (GLint)gb->height,
                          0, 0, (GLint)gb->width, (GLint)gb->height,
                          GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        // Restore both READ and DRAW to the viewport FBO
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFBO);
    }

    glActiveTexture(GL_TEXTURE0);
}

//=====================================================================================================================
// Deferred rendering pipeline - lighting
//=====================================================================================================================
// TODO: Implement dedicated lighting data UBO/SSBO for flexible light management

//=====================================================================================================================
// Default archetype render — forward pass for archetypes with no custom render callback
//=====================================================================================================================

// Cached field binding for rendererDefaultArchetypeRender — avoids per-frame strcmp
typedef struct
{
    StructLayout *layout;  // key: if layout changes, rebind
    i32 posXIdx, posYIdx, posZIdx;  // SoA position components (f32 each)
    i32 rotIdx, scaleIdx, modelIdx, aliveIdx;  // combined Vec4/Vec3 fields
    i32 rotXIdx, rotYIdx, rotZIdx, rotWIdx;    // split rotation components (f32 each)
    i32 scaleXIdx, scaleYIdx, scaleZIdx;       // split scale components (f32 each)
    b8  splitRot;    // true when RotationX/Y/Z/W are separate f32 fields
    b8  splitScale;  // true when ScaleX/Y/Z are separate f32 fields
} RenderFieldCache;

#define MAX_RENDER_FIELD_CACHES 32
static RenderFieldCache s_renderFieldCaches[MAX_RENDER_FIELD_CACHES];
static u32 s_renderFieldCacheCount = 0;

static RenderFieldCache *getRenderFieldCache(StructLayout *layout)
{
    // check existing
    for (u32 i = 0; i < s_renderFieldCacheCount; i++)
    {
        if (s_renderFieldCaches[i].layout == layout)
            return &s_renderFieldCaches[i];
    }

    // build new
    if (s_renderFieldCacheCount >= MAX_RENDER_FIELD_CACHES)
        return NULL;

    RenderFieldCache *c = &s_renderFieldCaches[s_renderFieldCacheCount++];
    c->layout = layout;
    c->posXIdx = c->posYIdx = c->posZIdx = -1;
    c->rotIdx = c->scaleIdx = c->modelIdx = c->aliveIdx = -1;
    c->rotXIdx = c->rotYIdx = c->rotZIdx = c->rotWIdx = -1;
    c->scaleXIdx = c->scaleYIdx = c->scaleZIdx = -1;
    c->splitRot = false;
    c->splitScale = false;

    for (u32 f = 0; f < layout->count; f++)
    {
        const c8 *name = layout->fields[f].name;
        u32 size = layout->fields[f].size;
        if      (strcmp(name, "PositionX")  == 0 && size == sizeof(f32))  c->posXIdx   = (i32)f;
        else if (strcmp(name, "PositionY")  == 0 && size == sizeof(f32))  c->posYIdx   = (i32)f;
        else if (strcmp(name, "PositionZ")  == 0 && size == sizeof(f32))  c->posZIdx   = (i32)f;
        else if (strcmp(name, "Rotation")   == 0 && size == sizeof(Vec4)) c->rotIdx    = (i32)f;
        else if (strcmp(name, "RotationX")  == 0 && size == sizeof(f32))  c->rotXIdx   = (i32)f;
        else if (strcmp(name, "RotationY")  == 0 && size == sizeof(f32))  c->rotYIdx   = (i32)f;
        else if (strcmp(name, "RotationZ")  == 0 && size == sizeof(f32))  c->rotZIdx   = (i32)f;
        else if (strcmp(name, "RotationW")  == 0 && size == sizeof(f32))  c->rotWIdx   = (i32)f;
        else if (strcmp(name, "Scale")      == 0 && size == sizeof(Vec3)) c->scaleIdx  = (i32)f;
        else if (strcmp(name, "ScaleX")     == 0 && size == sizeof(f32))  c->scaleXIdx = (i32)f;
        else if (strcmp(name, "ScaleY")     == 0 && size == sizeof(f32))  c->scaleYIdx = (i32)f;
        else if (strcmp(name, "ScaleZ")     == 0 && size == sizeof(f32))  c->scaleZIdx = (i32)f;
        else if (strcmp(name, "ModelID")    == 0 && size == sizeof(u32))  c->modelIdx  = (i32)f;
        else if (strcmp(name, "Alive")      == 0 && size == sizeof(b8))   c->aliveIdx  = (i32)f;
    }
    // Prefer split fields over combined when both exist
    if (c->rotXIdx >= 0 && c->rotYIdx >= 0 && c->rotZIdx >= 0 && c->rotWIdx >= 0)
        c->splitRot = true;
    if (c->scaleXIdx >= 0 && c->scaleYIdx >= 0 && c->scaleZIdx >= 0)
        c->splitScale = true;
    return c;
}

//=====================================================================================================================
// Frustum culling helpers (Gribb-Hartmann method, column-major VP matrix)
// vp.m[col][row] — extract planes so that dot(plane, worldPos) >= 0 is inside
//=====================================================================================================================
static void frustumExtract(const Mat4 *vp, Frustum *out)
{
    // Column-major: vp->m[col][row]
    // Left:   clip.x + clip.w >= 0   →  row0 + row3
    // Right:  clip.w - clip.x >= 0   →  row3 - row0
    // Bottom: clip.y + clip.w >= 0
    // Top:    clip.w - clip.y >= 0
    // Near:   clip.z + clip.w >= 0
    // Far:    clip.w - clip.z >= 0
    for (u32 c = 0; c < 3; c++)
    {
        u32 base = c * 2;
        // + side
        out->p[base  ].a = vp->m[0][c] + vp->m[0][3];
        out->p[base  ].b = vp->m[1][c] + vp->m[1][3];
        out->p[base  ].c = vp->m[2][c] + vp->m[2][3];
        out->p[base  ].d = vp->m[3][c] + vp->m[3][3];
        // - side
        out->p[base+1].a = vp->m[0][3] - vp->m[0][c];
        out->p[base+1].b = vp->m[1][3] - vp->m[1][c];
        out->p[base+1].c = vp->m[2][3] - vp->m[2][c];
        out->p[base+1].d = vp->m[3][3] - vp->m[3][c];
    }
    // Normalize planes so the distance formula works for sphere testing
    for (u32 i = 0; i < 6; i++)
    {
        f32 len = sqrtf(out->p[i].a*out->p[i].a + out->p[i].b*out->p[i].b + out->p[i].c*out->p[i].c);
        if (len > 1e-6f) { f32 inv = 1.0f/len; out->p[i].a*=inv; out->p[i].b*=inv; out->p[i].c*=inv; out->p[i].d*=inv; }
    }
}

// Returns false when the sphere is entirely outside at least one plane → cull it
static b8 frustumTestSphere(const Frustum *f, f32 cx, f32 cy, f32 cz, f32 r)
{
    for (u32 i = 0; i < 6; i++)
        if (f->p[i].a*cx + f->p[i].b*cy + f->p[i].c*cz + f->p[i].d < -r)
            return false;
    return true;
}

// Instanced draw for one mesh — handles buffered and standalone meshes
static void drawMeshInstanced(Mesh *mesh, u32 instanceCount)
{
    if (!mesh || mesh->vao == 0 || mesh->drawCount == 0 || instanceCount == 0) return;
    if (mesh->vao != s_lastBoundVAO) { glBindVertexArray(mesh->vao); s_lastBoundVAO = mesh->vao; profileCountVAOBind(); }
    if (mesh->buffered)
        glDrawElementsInstancedBaseVertex(GL_TRIANGLES, (GLsizei)mesh->drawCount,
            GL_UNSIGNED_INT, (void*)(uintptr_t)((u64)mesh->firstIndex * sizeof(u32)),
            (GLsizei)instanceCount, (GLint)mesh->baseVertex);
    else
    {
        if (mesh->ebo) glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mesh->drawCount, GL_UNSIGNED_INT, 0, (GLsizei)instanceCount);
        else           glDrawArraysInstanced(GL_TRIANGLES, 0, (GLsizei)mesh->drawCount, (GLsizei)instanceCount);
    }
    g_drawCalls++;
    g_triangles += (mesh->drawCount / 3) * instanceCount;
    g_vertices  += mesh->drawCount * instanceCount;
}

void rendererDefaultArchetypeRender(Archetype *arch, Renderer *r)
{
    if (!arch || !r || !arch->layout || !resources) return;

    RenderFieldCache *fc = getRenderFieldCache(arch->layout);
    if (!fc) return;

    b8 hasRot   = fc->splitRot   || fc->rotIdx   >= 0;
    b8 hasScale = fc->splitScale || fc->scaleIdx >= 0;
    if (fc->posXIdx < 0 || fc->posYIdx < 0 || fc->posZIdx < 0
        || !hasRot || !hasScale || fc->modelIdx < 0) return;
    if (arch->activeChunkCount == 0) return;

    u32 shader = r->defaultShader;
    if (shader == 0) return;

    glUseProgram(shader);
    profileCountShaderBind();
    MaterialUniforms matUniforms = getMaterialUniforms(shader);

    // frustum: use cached per-frame extraction from rendererBeginFrame
    const Frustum *frustum = &r->frustum;
    b8 hasFrustum = r->hasFrustum;

    // sub-pixel cull: skip entities whose screen-space size < MIN_PIXEL_SIZE
    // screenDiameter = (2 * worldRadius / distance) * projScale * (screenHeight / 2)
    //                = worldRadius * projScale * screenHeight / distance
    // Cull when: worldRadius * pixelFactor < MIN_PIXEL_SIZE * distance
    // → worldRadius * pixelFactor < MIN_PIXEL_SIZE * distance
    #define MIN_PIXEL_SIZE 1.5f
    f32 pixelFactor = 0.0f;
    f32 camPosX = 0, camPosY = 0, camPosZ = 0;
    b8  doPixelCull = false;
    if (hasFrustum && r->activeCamera != (u32)-1)
    {
        Camera *cam = (Camera *)bufferGet(&r->cameras, r->activeCamera);
        if (cam && r->display)
        {
            // projection.m[1][1] = 1/tan(fov/2) for perspective projection
            pixelFactor = cam->projection.m[1][1] * r->display->screenHeight;
            camPosX = cam->pos.x; camPosY = cam->pos.y; camPosZ = cam->pos.z;
            doPixelCull = (pixelFactor > 0.0f);
        }
    }

    // instanced path (batches entities by model ID into the SSBO)
    //
    // SINGLE-PASS design with O(1) model group lookup and per-group index lists.
    //
    // Phase A (single pass over all entities):
    //   Read pos + scale + modelID + alive.  Frustum cull ONCE.  O(1) group lookup
    //   via modelToGroup[4096].  Append packed (chunk << 20 | index) directly into
    //   the group's contiguous index list.  No scatter step needed.
    //
    // Phase B (iterate only VISIBLE entities, per group):
    //   Walk each group's index list, read rot/pos/scale, compute fused TRS matrix,
    //   write sequentially to SSBO.  Only visible entities are touched.
    //
    // Savings at 1M entities, ~44 models:
    //   - One pass instead of two: eliminates ~1M redundant field reads + frustum tests
    //   - O(1) group lookup vs O(44) linear scan = ~44M comparisons eliminated
    //   - No scatter step: indices land in per-group order during Phase A
    //   - Fused TRS: one combined matrix write instead of 3 separate Mat4 ops
    //   - Single heap allocation instead of three
    InstanceBuffer *ibuf = NULL;
    u32 ibSlot = r->defaultIBuffer;
    if (ibSlot != (u32)-1 && bufferIsOccupied(&r->instanceBuffers, ibSlot))
        ibuf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, ibSlot);

    // Cache uniform locations per shader to avoid per-frame driver queries
    static u32 s_cachedShader = 0;
    static i32 s_modelBaseLoc = -1;
    static i32 s_modelIndexLoc = -1;
    if (shader != s_cachedShader)
    {
        s_cachedShader   = shader;
        s_modelBaseLoc   = glGetUniformLocation(shader, "u_modelBaseIndex");
        s_modelIndexLoc  = glGetUniformLocation(shader, "u_modelIndex");
    }
    i32 modelBaseLoc = s_modelBaseLoc;
    b8  canInstance  = ibuf && ibuf->ready && modelBaseLoc != -1;

    // One-shot diagnostic: print instancing state on first call
    {
    }

    if (canInstance)
    {
        // Bind the current write buffer's SSBO to slot 1 (matches layout binding in shader)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ibuf->buffer[ibuf->writeIdx]);

        #define MAX_INST_GROUPS   256
        #define MODEL_LUT_SIZE   4096

        // Direct-index lookup: modelID -> group index.  O(1) instead of O(g) scan.
        // 0xFFFF = "not yet assigned a group".  Supports model IDs up to 4095.
        u16 modelToGroup[MODEL_LUT_SIZE];
        memset(modelToGroup, 0xFF, sizeof(modelToGroup));

        u32 uniqueModels[MAX_INST_GROUPS];
        u32 groupCounts [MAX_INST_GROUPS];
        u32 numGroups = 0;

        u32 maxAvail = ibuf->capacity - ibuf->count;

        // Single allocation for per-group index lists.
        // Each visible entity stores a packed u32: (chunk_index << 20) | entity_index.
        // Groups share this flat buffer — groupStarts[g] points to the start of group g's
        // region, filled during Phase A.  No post-pass scatter needed.
        static u32 *groupIndices = NULL;
        static u32  groupIndicesCap = 0;
        if (groupIndicesCap < maxAvail)
        {
            free(groupIndices);
            groupIndices = (u32 *)malloc(maxAvail * sizeof(u32));
            groupIndicesCap = groupIndices ? maxAvail : 0;
        }
        if (!groupIndices)
        {
            canInstance = false;
            goto per_entity_fallback;
        }

        u32 totalVisible = 0;
        b8  overflow = false;

        // Pre-compute total entity count and allocate flat visibility buffer
        // so physics can read frustum cull results via renderer->frameVisible
        u32 totalEntityCount = 0;
        for (u32 ch = 0; ch < arch->activeChunkCount; ch++)
            totalEntityCount += arch->arena[ch].count;
        if (totalEntityCount > 0)
        {
            r->frameVisible = (b8 *)frameAlloc(totalEntityCount * sizeof(b8));
            r->frameVisibleCount = totalEntityCount;
        }
        u32 visOffset = 0;

        // --- Phase A: two-step visibility + grouping ---
        //
        // Step 1 (OpenMP parallel): frustum cull + alive check → write per-entity
        //         visibility byte.  Each thread reads pos/scale/modelID/alive (read-only)
        //         and writes only to its own visibility[i].  Zero shared state.
        //
        // Step 2 (sequential): iterate visible entities, assign groups via modelToGroup
        //         LUT, build packed index + group tag arrays.
        //
        // Splitting lets the expensive frustum math scale across all cores while the
        // cheap grouping pass stays lock-free and branch-predictable.

        static u8  *entityGroupTag = NULL;
        static u32  entityGroupTagCap = 0;
        if (entityGroupTagCap < maxAvail)
        {
            free(entityGroupTag);
            entityGroupTag = (u8 *)malloc(maxAvail * sizeof(u8));
            entityGroupTagCap = entityGroupTag ? maxAvail : 0;
        }
        if (!entityGroupTag)
        {
            canInstance = false;
            goto per_entity_fallback;
        }

        // --- Step 1: parallel frustum cull per chunk ---
        for (u32 ch = 0; ch < arch->activeChunkCount; ch++)
        {
            void **fields = getArchetypeFields(arch, ch);
            if (!fields) continue;
            u32 count = arch->arena[ch].count;
            if (count == 0) continue;

            f32  *posX  = (f32  *)fields[fc->posXIdx];
            f32  *posY  = (f32  *)fields[fc->posYIdx];
            f32  *posZ  = (f32  *)fields[fc->posZIdx];
            Vec3 *scales = fc->splitScale ? NULL : (Vec3 *)fields[fc->scaleIdx];
            f32  *sclX   = fc->splitScale ? (f32 *)fields[fc->scaleXIdx] : NULL;
            f32  *sclY   = fc->splitScale ? (f32 *)fields[fc->scaleYIdx] : NULL;
            f32  *sclZ   = fc->splitScale ? (f32 *)fields[fc->scaleZIdx] : NULL;
            u32  *mids   = (u32  *)fields[fc->modelIdx];
            b8   *alive  = (fc->aliveIdx >= 0) ? (b8 *)fields[fc->aliveIdx] : NULL;

            // Per-entity visibility: 0 = culled, 1 = visible (reuse alive array space? no — write separate)
            // Use a static buffer sized to max chunk capacity
            static b8  *visBuf = NULL;
            static u32  visBufCap = 0;
            if (visBufCap < count)
            {
                free(visBuf);
                visBuf = (b8 *)malloc(count * sizeof(b8));
                visBufCap = visBuf ? count : 0;
            }
            if (!visBuf) { canInstance = false; goto per_entity_fallback; }

            const u32 modelUsed = resources->modelUsed;
            const Model *modelBuf = resources->modelBuffer;
            const b8 doFrustum = hasFrustum;
            const b8 doSplitScale = fc->splitScale;

            // Parallel frustum + sub-pixel cull: each thread writes visBuf[i] independently
            const b8 _doPixelCull = doPixelCull;
            const f32 _pixelFactor = pixelFactor;
            const f32 _camPX = camPosX, _camPY = camPosY, _camPZ = camPosZ;

            #pragma omp parallel for schedule(static) if(count > 50000)
            for (i32 i = 0; i < (i32)count; i++)
            {
                if (alive && !alive[i]) { visBuf[i] = false; continue; }
                u32 mid = mids[i];
                if (mid == (u32)-1 || mid >= modelUsed || mid >= MODEL_LUT_SIZE) { visBuf[i] = false; continue; }

                f32 sx = doSplitScale ? sclX[i] : scales[i].x;
                f32 sy = doSplitScale ? sclY[i] : scales[i].y;
                f32 sz = doSplitScale ? sclZ[i] : scales[i].z;
                f32 rad = sx; if (sy > rad) rad = sy; if (sz > rad) rad = sz;
                f32 br = modelBuf[mid].boundingRadius;
                if (br > 0.0f) rad *= br;

                if (doFrustum)
                {
                    if (!frustumTestSphere(frustum, posX[i], posY[i], posZ[i], rad)) { visBuf[i] = false; continue; }
                }

                // Sub-pixel cull: if projected screen size < MIN_PIXEL_SIZE pixels, skip
                // screenDiam ≈ rad * pixelFactor / distance
                // Cull when: rad * pixelFactor < MIN_PIXEL_SIZE * distance
                if (_doPixelCull)
                {
                    f32 dx = posX[i] - _camPX, dy = posY[i] - _camPY, dz = posZ[i] - _camPZ;
                    f32 distSq = dx*dx + dy*dy + dz*dz;
                    f32 thresh = MIN_PIXEL_SIZE * MIN_PIXEL_SIZE;
                    // Compare: (rad * pixelFactor)^2 < thresh * distSq  (avoid sqrt)
                    f32 rpf = rad * _pixelFactor;
                    if (rpf * rpf < thresh * distSq) { visBuf[i] = false; continue; }
                }

                visBuf[i] = true;
            }

            // Persist per-chunk visibility into flat buffer for physics LOD
            if (r->frameVisible && visOffset + count <= r->frameVisibleCount)
            {
                memcpy(r->frameVisible + visOffset, visBuf, count * sizeof(b8));
                visOffset += count;
            }

            // --- Step 2: sequential grouping of visible entities ---
            for (u32 i = 0; i < count; i++)
            {
                if (!visBuf[i]) continue;
                u32 mid = mids[i];

                // O(1) group lookup via direct-index table
                u16 g16 = modelToGroup[mid];
                if (g16 == 0xFFFF)
                {
                    if (numGroups >= MAX_INST_GROUPS) continue;
                    g16 = (u16)numGroups;
                    modelToGroup[mid] = g16;
                    uniqueModels[numGroups] = mid;
                    groupCounts[numGroups]  = 0;
                    numGroups++;
                }

                if (totalVisible >= maxAvail) { overflow = true; break; }

                groupIndices[totalVisible] = (ch << 20) | i;
                entityGroupTag[totalVisible] = (u8)g16;
                groupCounts[g16]++;
                totalVisible++;
            }
            if (overflow) break;
        }

        if (overflow)
        {
            canInstance = false;
            goto per_entity_fallback;
        }
        if (totalVisible == 0)
        {
            glUniform1i(modelBaseLoc, -1);
            return;
        }

        // --- Compute contiguous SSBO base offsets from exact group counts ---
        u32 groupBases[MAX_INST_GROUPS];
        u32 groupWriteOff[MAX_INST_GROUPS];   // per-group write cursor into sortedBuf
        {
            u32 ssboOff = ibuf->count;
            u32 sortOff = 0;
            for (u32 g = 0; g < numGroups; g++)
            {
                groupBases[g]    = ssboOff;
                groupWriteOff[g] = sortOff;
                ssboOff += groupCounts[g];
                sortOff += groupCounts[g];
            }
        }

        // --- Scatter packed indices into per-group contiguous order ---
        // This is a lightweight O(V) pass over visible entities only (NOT all 1M).
        // Each entry is 4 bytes; groups end up contiguous for sequential SSBO writes.
        static u32 *sortedBuf = NULL;
        static u32  sortedBufCap = 0;
        if (sortedBufCap < maxAvail)
        {
            free(sortedBuf);
            sortedBuf = (u32 *)malloc(maxAvail * sizeof(u32));
            sortedBufCap = sortedBuf ? maxAvail : 0;
        }
        if (!sortedBuf)
        {
            canInstance = false;
            goto per_entity_fallback;
        }

        for (u32 j = 0; j < totalVisible; j++)
        {
            u8 g = entityGroupTag[j];
            sortedBuf[groupWriteOff[g]++] = groupIndices[j];
        }

        // --- Phase B: write fused TRS transforms to SSBO (multithreaded) ---
        //
        // Flat parallel loop over ALL visible entities.  sortedBuf is already
        // arranged in per-group contiguous order and SSBO indices are sequential
        // (ibuf->count + j), so each thread writes to its own disjoint region —
        // no synchronisation needed.
        //
        // Each thread caches lastChunk locally to avoid redundant
        // getArchetypeFields calls when consecutive sorted entities share a chunk.
        {
            Mat4 *ssboFlat = &ibuf->data[ibuf->count];
            const i32 splitRot   = fc->splitRot;
            const i32 splitScale = fc->splitScale;
            const i32 piX = fc->posXIdx,  piY = fc->posYIdx,  piZ = fc->posZIdx;
            const i32 siX = fc->scaleXIdx, siY = fc->scaleYIdx, siZ = fc->scaleZIdx;
            const i32 siV = fc->scaleIdx;
            const i32 riX = fc->rotXIdx, riY = fc->rotYIdx, riZ = fc->rotZIdx, riW = fc->rotWIdx;
            const i32 riV = fc->rotIdx;

            #pragma omp parallel if(totalVisible > 50000)
            {
                u32 myLastChunk = (u32)-1;
                void **myFields = NULL;

                #pragma omp for schedule(static)
                for (i32 j = 0; j < (i32)totalVisible; j++)
                {
                    u32 packed = sortedBuf[j];
                    u32 ch = packed >> 20;
                    u32 ei = packed & 0xFFFFF;

                    if (ch != myLastChunk) { myFields = getArchetypeFields(arch, ch); myLastChunk = ch; }

                    f32 px = ((f32 *)myFields[piX])[ei];
                    f32 py = ((f32 *)myFields[piY])[ei];
                    f32 pz = ((f32 *)myFields[piZ])[ei];

                    f32 sx, sy, sz;
                    if (splitScale) {
                        sx = ((f32 *)myFields[siX])[ei];
                        sy = ((f32 *)myFields[siY])[ei];
                        sz = ((f32 *)myFields[siZ])[ei];
                    } else {
                        Vec3 s = ((Vec3 *)myFields[siV])[ei];
                        sx = s.x; sy = s.y; sz = s.z;
                    }

                    f32 qx, qy, qz, qw;
                    if (splitRot) {
                        qx = ((f32 *)myFields[riX])[ei];
                        qy = ((f32 *)myFields[riY])[ei];
                        qz = ((f32 *)myFields[riZ])[ei];
                        qw = ((f32 *)myFields[riW])[ei];
                    } else {
                        Vec4 q = ((Vec4 *)myFields[riV])[ei];
                        qx = q.x; qy = q.y; qz = q.z; qw = q.w;
                    }

                    // Fused TRS model matrix — T * R * S in one shot.
                    f32 xx = qx*qx, xy = qx*qy, xz = qx*qz, xw = qx*qw;
                    f32 yy = qy*qy, yz = qy*qz, yw = qy*qw;
                    f32 zz = qz*qz, zw = qz*qw;

                    f32 r00 = 1.0f - 2.0f*(yy+zz), r01 = 2.0f*(xy-zw), r02 = 2.0f*(xz+yw);
                    f32 r10 = 2.0f*(xy+zw), r11 = 1.0f - 2.0f*(xx+zz), r12 = 2.0f*(yz-xw);
                    f32 r20 = 2.0f*(xz-yw), r21 = 2.0f*(yz+xw), r22 = 1.0f - 2.0f*(xx+yy);

                    Mat4 *dst = &ssboFlat[j];
                    dst->m[0][0] = r00*sx; dst->m[0][1] = r10*sx; dst->m[0][2] = r20*sx; dst->m[0][3] = 0.0f;
                    dst->m[1][0] = r01*sy; dst->m[1][1] = r11*sy; dst->m[1][2] = r21*sy; dst->m[1][3] = 0.0f;
                    dst->m[2][0] = r02*sz; dst->m[2][1] = r12*sz; dst->m[2][2] = r22*sz; dst->m[2][3] = 0.0f;
                    dst->m[3][0] = px;     dst->m[3][1] = py;     dst->m[3][2] = pz;     dst->m[3][3] = 1.0f;
                }
            }
        }

        // Flush the written SSBO range so the GPU sees the new data.
        // glFlushMappedBufferRange + draw submission ordering is sufficient for
        // persistent-mapped buffers — no explicit glMemoryBarrier needed.
        instanceBufferFlushRange(ibuf, ibuf->count, totalVisible);

        ibuf->count += totalVisible;

        // --- Draw pass: one instanced draw call per mesh per unique model ---
        for (u32 g = 0; g < numGroups; g++)
        {
            if (groupCounts[g] == 0) continue;
            u32 mid = uniqueModels[g];
            if (mid >= resources->modelUsed) continue;
            Model *model = &resources->modelBuffer[mid];
            glUniform1i(modelBaseLoc, (i32)groupBases[g]);
            for (u32 m = 0; m < model->meshCount; m++)
            {
                u32 mi = model->meshIndices[m];
                if (mi >= resources->meshUsed) continue;
                Mesh *mesh = &resources->meshBuffer[mi];
                if (!mesh || mesh->vao == 0) continue;
                u32 matIdx = model->materialIndices[m];
                if (matIdx < resources->materialUsed)
                    updateMaterial(&resources->materialBuffer[matIdx], &matUniforms);
                drawMeshInstanced(mesh, groupCounts[g]);
            }
        }
        // Reset to single-draw mode for subsequent non-instanced calls
        glUniform1i(modelBaseLoc, -1);

        // Place a fence so the next frame's instanceBufferAdvance() knows when the
        // GPU has finished reading this buffer.  The fence is stored per-buffer-slot
        // and checked before overwriting.
        if (ibuf->fences[ibuf->writeIdx])
            glDeleteSync((GLsync)ibuf->fences[ibuf->writeIdx]);
        ibuf->fences[ibuf->writeIdx] = (void *)glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        return;
        #undef MAX_INST_GROUPS
        #undef MODEL_LUT_SIZE
    }

per_entity_fallback:;
    // per-entity fallback (no instancing shader or SSBO overflow)
    // Still applies frustum culling.
    if (modelBaseLoc != -1) glUniform1i(modelBaseLoc, -1);
    i32 modelIndexLoc = s_modelIndexLoc;  // use cached location
    b8 useSSBO = (r->modelSSBO != NULL) && (modelIndexLoc != -1);

    for (u32 ch = 0; ch < arch->activeChunkCount; ch++)
    {
        void **fields = getArchetypeFields(arch, ch);
        if (!fields) continue;
        u32 count = arch->arena[ch].count;
        if (count == 0) continue;

        f32  *posX    = (f32  *)fields[fc->posXIdx];
        f32  *posY    = (f32  *)fields[fc->posYIdx];
        f32  *posZ    = (f32  *)fields[fc->posZIdx];
        Vec4 *rotations = fc->splitRot   ? NULL : (Vec4 *)fields[fc->rotIdx];
        f32  *rotXa     = fc->splitRot   ? (f32 *)fields[fc->rotXIdx] : NULL;
        f32  *rotYa     = fc->splitRot   ? (f32 *)fields[fc->rotYIdx] : NULL;
        f32  *rotZa     = fc->splitRot   ? (f32 *)fields[fc->rotZIdx] : NULL;
        f32  *rotWa     = fc->splitRot   ? (f32 *)fields[fc->rotWIdx] : NULL;
        Vec3 *scales    = fc->splitScale ? NULL : (Vec3 *)fields[fc->scaleIdx];
        f32  *sclXa     = fc->splitScale ? (f32 *)fields[fc->scaleXIdx] : NULL;
        f32  *sclYa     = fc->splitScale ? (f32 *)fields[fc->scaleYIdx] : NULL;
        f32  *sclZa     = fc->splitScale ? (f32 *)fields[fc->scaleZIdx] : NULL;
        u32  *modelIDs  = (u32  *)fields[fc->modelIdx];
        b8   *alive     = (fc->aliveIdx >= 0) ? (b8 *)fields[fc->aliveIdx] : NULL;

        for (u32 i = 0; i < count; i++)
        {
            if (alive && !alive[i]) continue;
            u32 mid = modelIDs[i];
            if (mid == (u32)-1 || mid >= resources->modelUsed) continue;

            Vec4 rot = fc->splitRot   ? (Vec4){rotXa[i], rotYa[i], rotZa[i], rotWa[i]} : rotations[i];
            Vec3 scl = fc->splitScale ? (Vec3){sclXa[i], sclYa[i], sclZa[i]}           : scales[i];
            Model *model = &resources->modelBuffer[mid];
            {
                f32 rad = scl.x; if (scl.y > rad) rad = scl.y; if (scl.z > rad) rad = scl.z;
                f32 br = model->boundingRadius;
                if (br > 0.0f) rad *= br;
                if (hasFrustum && !frustumTestSphere(frustum, posX[i], posY[i], posZ[i], rad)) continue;
                if (doPixelCull)
                {
                    f32 dx = posX[i] - camPosX, dy = posY[i] - camPosY, dz = posZ[i] - camPosZ;
                    f32 distSq = dx*dx + dy*dy + dz*dz;
                    f32 rpf = rad * pixelFactor;
                    if (rpf * rpf < MIN_PIXEL_SIZE * MIN_PIXEL_SIZE * distSq) continue;
                }
            }
            Transform t = { {posX[i], posY[i], posZ[i]}, rot, scl };
            if (useSSBO) { u32 si = modelSSBOWrite(r->modelSSBO, &t); if (si != (u32)-1) glUniform1i(modelIndexLoc, (i32)si); else updateShaderModel(shader, t); }
            else updateShaderModel(shader, t);

            for (u32 m = 0; m < model->meshCount; m++)
            {
                u32 mi = model->meshIndices[m];
                if (mi >= resources->meshUsed) continue;
                Mesh *mesh = &resources->meshBuffer[mi];
                if (!mesh || mesh->vao == 0) continue;
                u32 matIdx = model->materialIndices[m];
                if (matIdx < resources->materialUsed)
                    updateMaterial(&resources->materialBuffer[matIdx], &matUniforms);
                drawMesh(mesh);
            }
        }
    }
}

//=====================================================================================================================
// Direct instanced mesh submission
// rendererSubmitInstance — queue one entity; rendererFlushInstancedModels — batch draw all queued
//=====================================================================================================================

// Per-model submission queue — lives in static storage, reset each flush
#define SUBMIT_MAX_GROUPS  256
#define SUBMIT_MAX_TOTAL   131072   // must be <= RENDERER_MAX_INSTANCE

typedef struct { u32 modelID; u32 base; u32 count; } SubmitGroup;
static SubmitGroup s_submitGroups[SUBMIT_MAX_GROUPS];
static u32         s_submitGroupCount = 0;
static u32         s_submitTotal = 0;

// Flat submission list — (modelID, Mat4) pairs, appended in any order.
// Flush groups them by modelID in a single pass using the group count/base table.
typedef struct { u32 modelID; Mat4 mat; } SubmitEntry;
static SubmitEntry s_submitList[SUBMIT_MAX_TOTAL];

void rendererSubmitInstance(Renderer *r, u32 modelID, const Transform *t)
{
    if (!r || !t || modelID == (u32)-1 || s_submitTotal >= SUBMIT_MAX_TOTAL) return;
    if (!resources || modelID >= resources->modelUsed) return;

    s_submitList[s_submitTotal].modelID = modelID;
    s_submitList[s_submitTotal].mat     = getModel(t);
    s_submitTotal++;

    // Track count per group (base is assigned at flush)
    u32 g = s_submitGroupCount;
    for (u32 k = 0; k < s_submitGroupCount; k++)
        if (s_submitGroups[k].modelID == modelID) { g = k; break; }
    if (g == s_submitGroupCount)
    {
        if (s_submitGroupCount >= SUBMIT_MAX_GROUPS) { s_submitTotal--; return; }
        s_submitGroups[g].modelID = modelID;
        s_submitGroups[g].count   = 0;
        s_submitGroups[g].base    = 0; // assigned at flush
        s_submitGroupCount++;
    }
    s_submitGroups[g].count++;
}

void rendererFlushInstancedModels(Renderer *r)
{
    if (!r || s_submitGroupCount == 0 || s_submitTotal == 0) return;

    u32 shader = r->defaultShader;
    if (shader == 0) { s_submitGroupCount = 0; s_submitTotal = 0; return; }

    // Get default instance buffer
    u32 ibSlot = r->defaultIBuffer;
    if (ibSlot == (u32)-1 || !bufferIsOccupied(&r->instanceBuffers, ibSlot)) { s_submitGroupCount = 0; s_submitTotal = 0; return; }
    InstanceBuffer *ibuf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, ibSlot);
    if (!ibuf || !ibuf->ready) { s_submitGroupCount = 0; s_submitTotal = 0; return; }

    // Use cached uniform location (populated in rendererDefaultArchetypeRender)
    static u32 s_cachedFlushShader = 0;
    static i32 s_flushModelBaseLoc = -1;
    if (shader != s_cachedFlushShader)
    {
        s_cachedFlushShader = shader;
        s_flushModelBaseLoc = glGetUniformLocation(shader, "u_modelBaseIndex");
    }
    i32 modelBaseLoc = s_flushModelBaseLoc;
    if (modelBaseLoc == -1 || ibuf->count + s_submitTotal > ibuf->capacity)
    {
        s_submitGroupCount = 0; s_submitTotal = 0;
        return;
    }

    // Assign SSBO base offsets per group, then scatter transforms into contiguous slots
    u32 ssboBase = ibuf->count;
    u32 running = 0;
    for (u32 g = 0; g < s_submitGroupCount; g++)
    {
        s_submitGroups[g].base = ssboBase + running;
        running += s_submitGroups[g].count;
        s_submitGroups[g].count = 0; // reuse as write cursor below
    }
    // Write each submitted entry into its group's contiguous slot
    for (u32 i = 0; i < s_submitTotal; i++)
    {
        u32 mid = s_submitList[i].modelID;
        for (u32 g = 0; g < s_submitGroupCount; g++)
        {
            if (s_submitGroups[g].modelID == mid)
            {
                ibuf->data[s_submitGroups[g].base + s_submitGroups[g].count] = s_submitList[i].mat;
                s_submitGroups[g].count++;
                break;
            }
        }
    }
    // Flush the written range so the GPU sees the new data (explicit flush mode)
    instanceBufferFlushRange(ibuf, ssboBase, s_submitTotal);

    // Also flush model SSBO if active
    if (r->modelSSBO)
    {
        modelSSBOEndFrame(r->modelSSBO);
    }

    ibuf->count += s_submitTotal;

    glUseProgram(shader);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ibuf->buffer[ibuf->writeIdx]);
    MaterialUniforms matUniforms = getMaterialUniforms(shader);

    //=====================================================================================================================
    // INDIRECT RENDERING PATH — build indirect command buffer instead of per-model loops
    //=====================================================================================================================
    if (r->indirectBuffer)
    {
        indirectBufferReset(r->indirectBuffer);
        
        // Build indirect command for each mesh in each model group
        for (u32 g = 0; g < s_submitGroupCount; g++)
        {
            u32 mid = s_submitGroups[g].modelID;
            u32 instanceCount = s_submitGroups[g].count;
            if (instanceCount == 0 || mid >= resources->modelUsed) continue;

            Model *model = &resources->modelBuffer[mid];
            u32 modelSSBOBase = s_submitGroups[g].base;

            // Add one indirect command per mesh in this model
            for (u32 m = 0; m < model->meshCount; m++)
            {
                u32 mi = model->meshIndices[m];
                if (mi >= resources->meshUsed) continue;

                Mesh *mesh = &resources->meshBuffer[mi];
                if (!mesh || mesh->vao == 0) continue;

                // Add command: one draw call for this mesh with all instances using shared model matrices
                indirectBufferAddCommand(r->indirectBuffer, mid,
                                        mesh->firstIndex,      // firstIndex in global IBO
                                        mesh->drawCount,       // index count per instance
                                        mesh->baseVertex,      // baseVertex in global VBO
                                        modelSSBOBase);        // gl_BaseInstance offset

                // Update material for this mesh (deferred until dispatch if needed)
                u32 matIdx = model->materialIndices[m];
                if (matIdx < resources->materialUsed)
                    updateMaterial(&resources->materialBuffer[matIdx], &matUniforms);
            }
        }

        // Upload and dispatch all commands in one GPU call
        indirectBufferUpload(r->indirectBuffer);
        indirectBufferDispatch(r->indirectBuffer, shader);
    }
    else
    {
        // FALLBACK PATH — original per-model loop (if indirect buffer unavailable)
        for (u32 g = 0; g < s_submitGroupCount; g++)
        {
            u32 mid = s_submitGroups[g].modelID;
            u32 cnt = s_submitGroups[g].count;
            if (cnt == 0 || mid >= resources->modelUsed) continue;

            Model *model = &resources->modelBuffer[mid];
            glUniform1i(modelBaseLoc, (i32)(ssboBase + s_submitGroups[g].base));
            for (u32 m = 0; m < model->meshCount; m++)
            {
                u32 mi = model->meshIndices[m];
                if (mi >= resources->meshUsed) continue;
                Mesh *mesh = &resources->meshBuffer[mi];
                if (!mesh || mesh->vao == 0) continue;
                u32 matIdx = model->materialIndices[m];
                if (matIdx < resources->materialUsed)
                    updateMaterial(&resources->materialBuffer[matIdx], &matUniforms);
                drawMeshInstanced(mesh, cnt);
            }
        }

        // Fence after draw so next frame's advance() knows when GPU is done reading
        if (ibuf->fences[ibuf->writeIdx])
            glDeleteSync((GLsync)ibuf->fences[ibuf->writeIdx]);
        ibuf->fences[ibuf->writeIdx] = (void *)glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        glUniform1i(modelBaseLoc, -1);
    }

    s_submitGroupCount = 0;
    s_submitTotal = 0;
}

void rendererSetActiveCamera(Renderer *r, u32 index)
{
    if (!r || !bufferIsOccupied(&r->cameras, index))
    {
        WARN("rendererSetActiveCamera: slot %u is not occupied", index);
        return;
    }
    r->activeCamera = index;
}

Camera *rendererGetCamera(Renderer *r, u32 index)
{
    if (!r || !bufferIsOccupied(&r->cameras, index)) return NULL;
    return (Camera *)bufferGet(&r->cameras, index);
}

u32 rendererGetActiveCamera(Renderer *r)
{
    if (!r) return (u32)-1;
    return r->activeCamera;
}
