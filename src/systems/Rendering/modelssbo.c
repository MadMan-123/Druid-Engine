#include "../../../include/druid.h"

ModelSSBO *modelSSBOCreate(u32 capacity)
{
    if (capacity == 0) { ERROR("modelSSBOCreate: capacity must be > 0"); return NULL; }

    ModelSSBO *ssbo = (ModelSSBO *)dalloc(sizeof(ModelSSBO), MEM_TAG_RENDERER);
    if (!ssbo) { ERROR("modelSSBOCreate: alloc failed"); return NULL; }

    ssbo->capacity = capacity;
    ssbo->count    = 0;
    ssbo->data     = NULL;
    ssbo->buffer   = 0;

    // Use GL_MAP_FLUSH_EXPLICIT_BIT instead of GL_MAP_COHERENT_BIT.
    // Coherent mapping forces per-cacheline write-combining buffer drains on every
    // store, which stalls the CPU when writing 1M+ Mat4 transforms per frame.
    // Explicit flush lets us batch all writes into a single glFlushMappedBufferRange
    // call, which is significantly faster for large sequential writes.
    const GLbitfield storageFlags = GL_MAP_WRITE_BIT
                                  | GL_MAP_PERSISTENT_BIT
                                  | GL_DYNAMIC_STORAGE_BIT;
    const GLbitfield mapFlags     = GL_MAP_WRITE_BIT
                                  | GL_MAP_PERSISTENT_BIT
                                  | GL_MAP_FLUSH_EXPLICIT_BIT;
    const GLsizeiptr byteSize = (GLsizeiptr)(capacity * sizeof(Mat4));

    glGenBuffers(1, &ssbo->buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo->buffer);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER, byteSize, NULL, storageFlags);

    ssbo->data = (Mat4 *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, byteSize, mapFlags);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    if (!ssbo->data)
    {
        ERROR("modelSSBOCreate: glMapBufferRange failed (capacity=%u)", capacity);
        glDeleteBuffers(1, &ssbo->buffer);
        dfree(ssbo, sizeof(ModelSSBO), MEM_TAG_RENDERER);
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
    dfree(ssbo, sizeof(ModelSSBO), MEM_TAG_RENDERER);
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

void modelSSBOEndFrame(ModelSSBO *ssbo)
{
    if (!ssbo || !ssbo->buffer || ssbo->count == 0) return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo->buffer);
    glFlushMappedBufferRange(GL_SHADER_STORAGE_BUFFER,
        0,
        (GLsizeiptr)(ssbo->count * sizeof(Mat4)));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void modelSSBOBind(ModelSSBO *ssbo, u32 bindingPoint)
{
    if (!ssbo) return;
    bindSSBOBase(ssbo->buffer, bindingPoint);
}
