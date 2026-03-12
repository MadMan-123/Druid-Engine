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

    // acquire a default camera (slot 0) with the display aspect ratio
    f32 aspect = display->screenWidth / display->screenHeight;
    u32 camIdx = rendererAcquireCamera(r, (Vec3){0.0f, 2.0f, 8.0f}, fov, aspect, nearClip, farClip);
    r->activeCamera = camIdx;

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

    // free the buffers themselves
    bufferDestroy(&r->cameras);
    bufferDestroy(&r->instanceBuffers);
    bufferDestroy(&r->gBuffers);

    if (r->coreUBO)
        freeUBO(r->coreUBO);

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

void rendererSetActiveCamera(Renderer *r, u32 index)
{
    if (!r || !bufferIsOccupied(&r->cameras, index))
    {
        WARN("rendererSetActiveCamera: slot %u is not occupied", index);
        return;
    }
    r->activeCamera = index;
}
