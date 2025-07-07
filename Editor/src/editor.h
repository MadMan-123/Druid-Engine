#pragma once

#include <druid.h>
#include <SDL3/SDL.h>
#include <GL/glew.h>

// Forward declarations and global variables shared across the Editor
extern Application* editor;

// Shared rendering globals for the viewport
extern u32 viewportFBO;
extern u32 viewportTexture;
extern u32 viewportWidth;
extern u32 viewportHeight;
extern u32 depthRB;

// Skybox resources
extern Mesh* skyboxMesh;
extern u32 cubeMapTexture;
extern u32 skyboxShader;
extern u32 skyboxViewLoc;
extern u32 skyboxProjLoc;

// Scene camera accessible from multiple files
extern Camera sceneCam;

// Application lifecycle hooks implemented for the Editor
void processInput(void* appData);
void init();
void update(f32 dt);
void render(f32 dt);
void destroy();

// Editor UI drawing entry point (panels, dockspace, etc.)
void drawDockspaceAndPanels();

