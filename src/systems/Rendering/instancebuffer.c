#include "../../../include/druid.h"


void instanceBufferCreate(InstanceBuffer* buf, u32 capacity)
{
    buf->capacity = capacity;
    buf->count    = 0;
    buf->ready    = false;
    buf->data     = NULL;

    glGenBuffers(1, &buf->buffer);

    // Bind the buffer as a shader storage buffer 
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf->buffer);

    const GLbitfield flags = GL_MAP_WRITE_BIT
                           | GL_MAP_PERSISTENT_BIT
                           | GL_MAP_COHERENT_BIT
                           | GL_DYNAMIC_STORAGE_BIT;
    const GLsizeiptr size  = (GLsizeiptr)(capacity * sizeof(Mat4));

    // allocate the memory for the buffer
    glBufferStorage(GL_SHADER_STORAGE_BUFFER, size, NULL, flags);
    buf->data = (Mat4*)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, size,
                       GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    buf->ready = (buf->data != NULL);
}

void instanceBufferDestroy(InstanceBuffer* buf)
{
    if (buf->buffer)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf->buffer);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glDeleteBuffers(1, &buf->buffer);
    }
    buf->buffer      = 0;
    buf->data     = NULL;
    buf->capacity = 0;
    buf->count    = 0;
    buf->ready    = false;
}
