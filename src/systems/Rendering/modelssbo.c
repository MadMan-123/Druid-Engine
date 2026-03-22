#include "../../../include/druid.h"

ModelSSBO *modelSSBOCreate(u32 capacity)
{
    if (capacity == 0) { ERROR("modelSSBOCreate: capacity must be > 0"); return NULL; }

    ModelSSBO *ssbo = (ModelSSBO *)malloc(sizeof(ModelSSBO));
    if (!ssbo) { ERROR("modelSSBOCreate: malloc failed"); return NULL; }

    ssbo->capacity = capacity;
    ssbo->count    = 0;
    ssbo->data     = NULL;
    ssbo->buffer   = 0;

    const GLbitfield flags    = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT
                              | GL_MAP_COHERENT_BIT | GL_DYNAMIC_STORAGE_BIT;
    const GLsizeiptr byteSize = (GLsizeiptr)(capacity * sizeof(Mat4));

    glGenBuffers(1, &ssbo->buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo->buffer);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER, byteSize, NULL, flags);

    ssbo->data = (Mat4 *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, byteSize,
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    if (!ssbo->data)
    {
        ERROR("modelSSBOCreate: glMapBufferRange failed (capacity=%u)", capacity);
        glDeleteBuffers(1, &ssbo->buffer);
        free(ssbo);
        return NULL;
    }

    return ssbo;
}

void modelSSBODestroy(ModelSSBO *ssbo)
{
    if (!ssbo) return;
    if (ssbo->buffer)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo->buffer);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glDeleteBuffers(1, &ssbo->buffer);
    }
    free(ssbo);
}

void modelSSBOBeginFrame(ModelSSBO *ssbo)
{
    if (ssbo) ssbo->count = 0;
}

u32 modelSSBOWrite(ModelSSBO *ssbo, const Transform *t)
{
    if (!ssbo || !t) { ERROR("modelSSBOWrite: NULL argument"); return (u32)-1; }
    if (ssbo->count >= ssbo->capacity) { ERROR("modelSSBOWrite: buffer full"); return (u32)-1; }
    u32 index = ssbo->count++;
    ssbo->data[index] = getModel(t);
    return index;
}

void modelSSBOUpload(ModelSSBO *ssbo)
{
    if (!ssbo || !ssbo->buffer) return;
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void modelSSBOBind(ModelSSBO *ssbo, u32 bindingPoint)
{
    if (!ssbo) return;
    bindSSBOBase(ssbo->buffer, bindingPoint);
}
