#include "../../include/druid.h"

DAPI Framebuffer createFramebuffer(u32 width, u32 height, GLenum internalFormat, b32 hasDepth)
{
    Framebuffer fb = {0};
    fb.width = width;
    fb.height = height;
    fb.internalFormat = internalFormat;
    fb.hasDepth = hasDepth;

    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);

    glGenTextures(1, &fb.texture);
    glBindTexture(GL_TEXTURE_2D, fb.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.texture, 0);

    if (hasDepth)
    {
        glGenRenderbuffers(1, &fb.rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        ERROR("createFramebuffer: incomplete framebuffer 0x%X", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fb;
}

DAPI void resizeFramebuffer(Framebuffer *fb, u32 width, u32 height)
{
    if (!fb) return;
    if (fb->width == width && fb->height == height) return;
    fb->width = width; fb->height = height;

    // resize texture
    glBindTexture(GL_TEXTURE_2D, fb->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, fb->internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    if (fb->hasDepth && fb->rbo != 0)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, fb->rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

DAPI void bindFramebuffer(Framebuffer *fb)
{
    if (!fb) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glViewport(0,0, fb->width, fb->height);
}

DAPI void unbindFramebuffer(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

DAPI void destroyFramebuffer(Framebuffer *fb)
{
    if (!fb) return;
    if (fb->texture) glDeleteTextures(1, &fb->texture);
    if (fb->rbo) glDeleteRenderbuffers(1, &fb->rbo);
    if (fb->fbo) glDeleteFramebuffers(1, &fb->fbo);
    fb->fbo = fb->texture = fb->rbo = 0;
}
