#include "../../../include/druid.h"
#include <stdlib.h>
#include <string.h>

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

    Renderer *r = (Renderer *)malloc(sizeof(Renderer));
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
        free(r);
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

    r->activeGBuffer = (u32)-1;
    r->useDeferredRendering = false;

    r->activeCamera = (u32)-1; // no default camera — caller must acquire one

    // acquire a default instance buffer (slot 0)
    rendererAcquireInstanceBuffer(r, RENDERER_MAX_INSTANCE);

    // set the global pointer
    renderer = r;

    INFO("Renderer created (%gx%g, cams=%u, ibufs=%u, gbufs=%u)",
         display->screenWidth, display->screenHeight,
         maxCameras, maxIBuffers, maxGBuffers);
    return r;
}

void rendererBeginFrame(Renderer *r, f32 dt)
{
    if (!r) return;

    r->time += dt;

    // push active camera + timing into the core UBO so every shader sees them
    u32 ci = r->activeCamera;
    Camera *cam = (Camera *)bufferGet(&r->cameras, ci);
    if (cam && bufferIsOccupied(&r->cameras, ci))
    {
        Mat4 view = getView(cam, false);
        updateCoreShaderUBO(r->time, &cam->pos, &view, &cam->projection);
    }

    // reset all occupied instance buffers for this frame
    for (u32 i = 0; i < r->instanceBuffers.capacity; i++)
    {
        if (bufferIsOccupied(&r->instanceBuffers, i))
        {
            InstanceBuffer *buf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, i);
            buf->count = 0;
        }
    }

    // reset model SSBO write cursor and bind it to slot 2 for this frame
    if (r->modelSSBO)
    {
        modelSSBOBeginFrame(r->modelSSBO);
        modelSSBOBind(r->modelSSBO, 2);
    }
}

void rendererFlushInstances(Renderer *r, u32 ibufferIndex, u32 shaderProgram)
{
    if (!r) return;
    if (!bufferIsOccupied(&r->instanceBuffers, ibufferIndex)) return;

    InstanceBuffer *buf = (InstanceBuffer *)bufferGet(&r->instanceBuffers, ibufferIndex);
    if (!buf || !buf->ready || buf->count == 0) return;

    // bind the instance SSBO to binding point 1
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buf->buffer);
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

    if (renderer == r)
        renderer = NULL;

    free(r);
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

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(lightingShader);

    // Bind GBuffer textures to sequential texture units
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gb->positionTex);
    glUniform1i(glGetUniformLocation(lightingShader, "gPosition"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gb->normalTex);
    glUniform1i(glGetUniformLocation(lightingShader, "gNormal"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gb->albedoSpecTex);
    glUniform1i(glGetUniformLocation(lightingShader, "gAlbedoSpec"), 2);

    // Render full-screen quad
    // Use a simple quad: two triangles covering NDC [-1,1]
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

    glActiveTexture(GL_TEXTURE0);
}

//=====================================================================================================================
// Default archetype render — forward pass for archetypes with no custom render callback
//=====================================================================================================================

void rendererDefaultArchetypeRender(Archetype *arch, Renderer *r)
{
    if (!arch || !r || !arch->layout || !resources) return;

    // NOTE: The caller is responsible for binding/unbinding the GBuffer
    // (rendererBeginDeferredPass / rendererEndDeferredPass) when deferred
    // rendering is active. This function just draws geometry.

    StructLayout *layout = arch->layout;
    i32 posIdx   = -1;
    i32 rotIdx   = -1;
    i32 scaleIdx = -1;
    i32 modelIdx = -1;
    i32 aliveIdx = -1;

    // Find field indices by name and expected size
    for (u32 f = 0; f < layout->count; f++)
    {
        const c8 *name = layout->fields[f].name;
        u32 size = layout->fields[f].size;
        if (strcmp(name, "Position") == 0 && size == sizeof(Vec3))  posIdx   = (i32)f;
        else if (strcmp(name, "Rotation") == 0 && size == sizeof(Vec4))  rotIdx   = (i32)f;
        else if (strcmp(name, "Scale") == 0    && size == sizeof(Vec3))  scaleIdx = (i32)f;
        else if (strcmp(name, "ModelID") == 0  && size == sizeof(u32))   modelIdx = (i32)f;
        else if (strcmp(name, "Alive") == 0    && size == sizeof(b8))    aliveIdx = (i32)f;
    }

    // Non-renderable archetype (missing required transform/model fields)
    if (posIdx < 0 || rotIdx < 0 || scaleIdx < 0 || modelIdx < 0) return;

    void **fields = getArchetypeFields(arch, 0);
    if (!fields) return;

    u32 count = arch->arena[0].count;
    if (count == 0) return;

    Vec3 *positions = (Vec3 *)fields[posIdx];
    Vec4 *rotations = (Vec4 *)fields[rotIdx];
    Vec3 *scales    = (Vec3 *)fields[scaleIdx];
    u32  *modelIDs  = (u32  *)fields[modelIdx];
    b8   *alive     = (aliveIdx >= 0) ? (b8 *)fields[aliveIdx] : NULL;

    u32 shader = r->defaultShader;
    if (shader == 0) return;

    glUseProgram(shader);
    profileCountShaderBind();

    // Locate the u_modelIndex uniform once up front
    i32 modelIndexLoc = glGetUniformLocation(shader, "u_modelIndex");

    // Determine whether we can use the SSBO path
    b8 useSSBO = (r->modelSSBO != NULL) && (modelIndexLoc != -1);

    for (u32 i = 0; i < count; i++)
    {
        if (alive && !alive[i]) continue;

        u32 mid = modelIDs[i];
        if (mid == (u32)-1 || mid >= resources->modelUsed) continue;

        Model *model = &resources->modelBuffer[mid];
        Transform t = { positions[i], rotations[i], scales[i] };

        // Write the transform into the SSBO and tell the shader which slot to
        // read via u_modelIndex.  Fall back to the legacy per-draw uniform
        // upload when the SSBO is unavailable (e.g. old hardware / no slot).
        if (useSSBO)
        {
            u32 slotIndex = modelSSBOWrite(r->modelSSBO, &t);
            if (slotIndex != (u32)-1)
            {
                glUniform1i(modelIndexLoc, (i32)slotIndex);
            }
            else
            {
                // SSBO full this frame — fall back to direct uniform upload
                updateShaderModel(shader, t);
            }
        }
        else
        {
            updateShaderModel(shader, t);
        }

        for (u32 m = 0; m < model->meshCount; m++)
        {
            u32 mi = model->meshIndices[m];
            if (mi >= resources->meshUsed) continue;

            Mesh *mesh = &resources->meshBuffer[mi];
            if (!mesh || mesh->vao == 0) continue;

            u32 matIdx = model->materialIndices[m];
            if (matIdx >= resources->materialUsed) continue;

            MaterialUniforms uniforms = getMaterialUniforms(shader);
            updateMaterial(&resources->materialBuffer[matIdx], &uniforms);
            drawMesh(mesh);
        }
    }

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
