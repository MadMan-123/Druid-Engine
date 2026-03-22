#include "../../../include/druid.h"

u32 createSSBO(u32 size, const void *data, GLenum usage)
{
    GLuint buf = 0;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, usage);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return (u32)buf;
}

void updateSSBO(u32 ssbo, u32 offset, u32 size, const void *data)
{
    if (ssbo == 0) return;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void bindSSBOBase(u32 ssbo, u32 bindingPoint)
{
    if (ssbo == 0) return;
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, ssbo);
}

void destroySSBO(u32 ssbo)
{
    if (ssbo == 0) return;
    glDeleteBuffers(1, &ssbo);
}
