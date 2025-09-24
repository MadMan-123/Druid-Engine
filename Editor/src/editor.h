#pragma once

#include <druid.h>
#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <imgui.h>
#define MAX_CONSOLE_LINES 10000
#define MAX_CONSOLE_LINE_LENGTH 256

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
extern u32 *modelIDs;
extern Mesh* cubeMesh;
extern Material* materials;

extern bool canMoveViewPort;
extern bool manipulateTransform;
extern u32 shader;
extern f32 viewportWidthPixels;
extern f32 viewportHeightPixels;
extern f32 viewportOffsetX;
extern f32 viewportOffsetY;

extern ImVec2 g_viewportScreenPos;
extern ImVec2 g_viewportSize;

extern Mesh* cubeMesh;
extern u32 arrowShader;
extern u32 colourLocation;
extern bool canMoveAxis;
extern Vec3 manipulateAxis;
extern ResourceManager* resources;

typedef enum{
    MANIPULATE_POSITION = 0,
    MANIPULATE_ROTATION = 1,
    MANIPULATE_SCALE = 2,
    MANIPULATE_MAX = 3
}ManipulateTransformState;

enum InspectorState{
    EMPTY_VIEW = -1,
    ENTITY_VIEW = 0,  //view entities component data
    MAX_STATE
};
extern InspectorState currentInspectorState; 
extern u32 inspectorEntityID; //holds the index for the inspector to load component data  

extern ManipulateTransformState manipulateState;
extern const char** consoleLines;
// Application lifecycle hooks implemented for the Editor
void processInput(void* appData);
void init();
void update(f32 dt);
void render(f32 dt);
void destroy();

// Editor UI drawing entry point (panels, dockspace, etc.)
void drawDockspaceAndPanels();

void editorLog(LogLevel level, const char* msg);
