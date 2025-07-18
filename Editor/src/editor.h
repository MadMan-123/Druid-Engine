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
extern i32 entitySize;
extern u32 entitySizeCache;
extern u32 entityCount;

extern bool* isActive;
extern Vec3* positions;
extern Vec4* rotations;
extern Vec3* scales;
extern char* names;
extern char* meshNames;
extern Mesh* cubeMesh;
extern bool canMoveViewPort;

extern u32 shader;



enum InspectorState{
    EMPTY_VIEW = -1,
    ENTITY_VIEW = 0,  //view entities component data
    MAX_STATE
};
extern InspectorState currentInspectorState; 
extern u32 inspectorEntityID; //holds the index for the inspector to load component data  


// Application lifecycle hooks implemented for the Editor
void processInput(void* appData);
void init();
void update(f32 dt);
void render(f32 dt);
void destroy();

// Editor UI drawing entry point (panels, dockspace, etc.)
void drawDockspaceAndPanels();

