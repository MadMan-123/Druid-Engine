#pragma once
#include <druid.h>
#include <imgui/imgui.h>



#define ID_TYPE_ENTITY     (0 << 16) // 0x000000 — entity IDs live in lower 16 bits
#define ID_TYPE_GIZMO_X    (1 << 16) // 0x010000
#define ID_TYPE_GIZMO_Y    (2 << 16) // 0x020000
#define ID_TYPE_GIZMO_Z    (3 << 16) // 0x030000

#define ID_MASK_TYPE       0xFF0000
#define ID_MASK_ENTITY     0x00FFFF



extern u32 idFBO;
extern u32 idTexture;

extern u32 idShader;
extern u32 idLocation;
extern u32 idDepthRB;
typedef enum
{
    PICK_NONE = 0,
    PICK_ENTITY,
    PICK_GIZMO_X,
    PICK_GIZMO_Y,
    PICK_GIZMO_Z
} PickType;

enum AxisType
{
    AXIS_X,
    AXIS_Y,
    AXIS_Z
};

typedef struct{
    PickType type;
    union {
        u32 entityID;
    };
}PickResult;

void initIDFramebuffer();

void renderIDPass();

PickResult getEntityAtMouse(ImVec2 mousePos, ImVec2 viewportTopLeft);

void drawMeshIDPass(Mesh* mesh);


