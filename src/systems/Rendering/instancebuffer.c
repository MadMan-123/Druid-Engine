
#include "../../../include/druid.h"

//=====================================================================================================================
// Double-buffered instance SSBO
//
// Two persistently-mapped GL buffers rotate each frame.  CPU writes to buffer A
// while GPU reads from buffer B (drawn last frame).  A GLsync fence placed after
// each draw ensures we never overwrite data the GPU is still consuming.
//=====================================================================================================================

void instanceBufferCreate(InstanceBuffer* buf, u32 capacity)
{
    buf->capacity = capacity;
    buf->count    = 0;
    buf->writeIdx = 0;
    buf->ready    = false;
    buf->data     = NULL;

    const GLbitfield storageFlags = GL_MAP_WRITE_BIT
                                  | GL_MAP_PERSISTENT_BIT
                                  | GL_DYNAMIC_STORAGE_BIT;
    const GLbitfield mapFlags     = GL_MAP_WRITE_BIT
                                  | GL_MAP_PERSISTENT_BIT
                                  | GL_MAP_FLUSH_EXPLICIT_BIT;
    const GLsizeiptr size = (GLsizeiptr)(capacity * sizeof(Mat4));

    for (u32 i = 0; i < INST_BUF_COUNT; i++)
    {
        glGenBuffers(1, &buf->buffer[i]);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf->buffer[i]);
        glBufferStorage(GL_SHADER_STORAGE_BUFFER, size, NULL, storageFlags);
        buf->maps[i] = (Mat4 *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, size, mapFlags);
        buf->fences[i] = NULL;

        if (!buf->maps[i])
        {
            ERROR("instanceBufferCreate: glMapBufferRange failed for buffer %u", i);
            // clean up already-created buffers
            for (u32 j = 0; j <= i; j++)
            {
                if (buf->maps[j])
                {
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf->buffer[j]);
                    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
                }
                glDeleteBuffers(1, &buf->buffer[j]);
                buf->maps[j] = NULL;
                buf->buffer[j] = 0;
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            return;
        }
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    buf->data  = buf->maps[0];
    buf->ready = true;
}

void instanceBufferAdvance(InstanceBuffer* buf)
{
    if (!buf || !buf->ready) return;

    // Rotate to next buffer
    buf->writeIdx = (buf->writeIdx + 1) % INST_BUF_COUNT;

    // Wait on the fence for the buffer we're about to write to.
    // This ensures the GPU has finished reading from it (from 1-2 frames ago).
    // Use a generous timeout (50ms) and spin-wait if needed — blocking here is
    // correct behaviour (CPU must not overwrite data GPU is still reading).
    GLsync fence = (GLsync)buf->fences[buf->writeIdx];
    if (fence)
    {
        GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 50000000ULL); // 50ms
        if (result == GL_TIMEOUT_EXPIRED)
        {
            // GPU is very behind — do a full blocking wait
            result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 500000000ULL); // 500ms
        }
        glDeleteSync(fence);
        buf->fences[buf->writeIdx] = NULL;
    }

    // Point data to the new write buffer and reset count
    buf->data  = buf->maps[buf->writeIdx];
    buf->count = 0;
}

void instanceBufferFlushRange(InstanceBuffer* buf, u32 offset, u32 count)
{
    if (!buf || !buf->ready || count == 0) return;
    if (offset + count > buf->capacity) return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf->buffer[buf->writeIdx]);
    glFlushMappedBufferRange(GL_SHADER_STORAGE_BUFFER,
        (GLintptr)(offset * sizeof(Mat4)),
        (GLsizeiptr)(count * sizeof(Mat4)));
    // Do NOT unbind — caller may still need the binding active
}

void instanceBufferDestroy(InstanceBuffer* buf)
{
    for (u32 i = 0; i < INST_BUF_COUNT; i++)
    {
        if (buf->fences[i])
        {
            glDeleteSync((GLsync)buf->fences[i]);
            buf->fences[i] = NULL;
        }
        if (buf->buffer[i])
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf->buffer[i]);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            glDeleteBuffers(1, &buf->buffer[i]);
        }
        buf->buffer[i] = 0;
        buf->maps[i]   = NULL;
    }
    buf->data     = NULL;
    buf->capacity = 0;
    buf->count    = 0;
    buf->ready    = false;
}
