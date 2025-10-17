#include "../include/druid.h"

// CoreShaderData UBO - std140 friendly layout


typedef struct{
    f32 camPos[3];
    f32 time;
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

void updateCoreShaderUBO(f32 timeSeconds, const Vec3 *camPos)
{
    if (g_coreUBO == 0) createCoreShaderUBO();
    CoreShaderData data;
    data.time = timeSeconds;
    if (camPos)
    {
        data.camPos[0] = camPos->x;
        data.camPos[1] = camPos->y;
        data.camPos[2] = camPos->z;
    }
    //data.pad0 = data.pad1 = data.pad2 = 0.0f;

    updateUBO(g_coreUBO, 0, sizeof(CoreShaderData), &data);
}
