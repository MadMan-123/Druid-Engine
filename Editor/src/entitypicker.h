#pragma once
#include <druid.h>
#include <imgui/imgui.h>

void initIDFramebuffer();

extern u32 idFBO;
extern u32 idTexture;

extern u32 idShader;
extern u32 idLocation;
extern u32 idDepthRB;
void initIDFramebuffer();

void renderIDPass();

u32 getEntityAtMouse(ImVec2 mousePos, ImVec2 viewportTopLeft);

