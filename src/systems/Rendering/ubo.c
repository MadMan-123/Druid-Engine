#include "../include/druid.h"

// CoreShaderData UBO - std140 friendly layout


typedef struct{
    f32 camPos[3];
    f32 time;

    // Camera data
    Mat4 view;
    Mat4 projection;
}CoreShaderData;


static u32 g_coreUBO = 0;

u32 createCoreShaderUBO()
{
    if (g_coreUBO != 0) return g_coreUBO;
    CoreShaderData data = {0};
    g_coreUBO = createUBO(sizeof(CoreShaderData), &data, GL_DYNAMIC_DRAW);
    // bind to base 0
    bindUBOBase(g_coreUBO, 0);
    return g_coreUBO;
}
// Simple UBO API
u32 createUBO(u32 size, const void *data, GLenum usage)
{
    GLuint buf = 0;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_UNIFORM_BUFFER, buf);
    glBufferData(GL_UNIFORM_BUFFER, size, data, usage);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return (u32)buf;
}

void updateUBO(u32 ubo, u32 offset, u32 size, const void *data)
{
    if (ubo == 0)
        return;
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void bindUBOBase(u32 ubo, u32 bindingPoint)
{
    if (ubo == 0)
        return;
    glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, ubo);
}

void updateCoreShaderUBO(f32 timeSeconds, const Vec3 *camPos, const Mat4 *view, const Mat4 *projection)
{
    if (g_coreUBO == 0) createCoreShaderUBO();
    CoreShaderData data;
    data.time = timeSeconds;
    if (view)
        data.view = *view;
    if (projection)
        data.projection = *projection;
    {
        data.camPos[0] = camPos->x;
        data.camPos[1] = camPos->y;
        data.camPos[2] = camPos->z;
    }
    //data.pad0 = data.pad1 = data.pad2 = 0.0f;

    updateUBO(g_coreUBO, 0, sizeof(CoreShaderData), &data);
}

