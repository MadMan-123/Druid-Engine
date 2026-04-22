#pragma once

#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <imgui.h>
#include <druid.h>
#include "project_builder.h"
#define MAX_CONSOLE_LINES 10000
#define MAX_CONSOLE_LINE_LENGTH 256
#define DRUID_PROFILE 1 // Set to 1 to enable profiling, 0 to disable

// Forward declarations and global variables shared across the Editor
extern Application* editor;

// SceneEntity archetype (defined in editor.cpp)
extern FieldInfo SceneEntity_fields[];
extern StructLayout SceneEntity;

// Shared rendering globals for the viewport - Multi-FBO system
#define MAX_FBOS 5
#define ID_FBO_INDEX 4  // Last FBO slot for ID picking
extern Framebuffer viewportFBs[MAX_FBOS];
extern Framebuffer finalDisplayFB;
extern u32 viewportWidth;
extern u32 viewportHeight;
extern u32 activeFBO;

// Screen quad for FBO rendering
extern Mesh* screenQuadMesh;

// Skybox resources
extern Mesh* skyboxMesh;
extern u32 cubeMapTexture;
extern u32 skyboxShader;
extern u32 skyboxViewLoc;
extern u32 skyboxProjLoc;

// Scene camera accessible from multiple files
extern Camera sceneCam;
extern u32 g_editorCamSlot; // renderer camera slot
extern b8 *sceneCameraFlags;
extern i32 entitySize;
extern u32 entitySizeCache;
extern u32 entityCount;
extern Archetype sceneArchetype;

extern b8* isActive;
extern Vec3* positions;
extern Vec4* rotations;
extern Vec3* scales;
extern c8* names;
extern u32 *modelIDs;
extern u32 *shaderHandles;
extern u32 *entityMaterialIDs;
extern u32 *archetypeIDs;
extern u64 *archetypeHashes;
extern u32 *ecsSlotIDs;
extern c8  *entityTags;
extern u32 *physicsBodyTypes;
extern f32 *masses;
extern u32 *colliderShapes;
extern f32 *sphereRadii;
extern f32 *colliderHalfXs;
extern f32 *colliderHalfYs;
extern f32 *colliderHalfZs;
extern b8  *isLight;
extern u32 *lightTypes;
extern f32 *lightRanges;
extern f32 *lightColorRs;
extern f32 *lightColorGs;
extern f32 *lightColorBs;
extern f32 *lightIntensities;
extern f32 *lightDirXs;
extern f32 *lightDirYs;
extern f32 *lightDirZs;
extern f32 *lightInnerCones;
extern f32 *lightOuterCones;
extern Mesh* cubeMesh;
extern Material* materials;

extern b8 canMoveViewPort;
extern b8 g_runtimeFreeView;
extern b8 manipulateTransform;
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
extern u32 fboShader;
extern u32 deferredLightingShader;
extern b8 canMoveAxis;
extern b8 showSkyboxSettings;
extern b8 showColliders;
extern Vec3 manipulateAxis;

extern c8 scenePathBuffer[512];


extern MaterialUniforms materialUniforms;
typedef enum{
    MANIPULATE_POSITION = 0,
    MANIPULATE_ROTATION = 1,
    MANIPULATE_SCALE = 2,
    MANIPULATE_MAX = 3
}ManipulateTransformState;

enum InspectorState{
    EMPTY_VIEW = -1,
    ENTITY_VIEW = 0,  //view entities component data
    SKYBOX_VIEW = 1,  //view skybox settings
    MAX_STATE
};
extern InspectorState currentInspectorState; 
extern u32 inspectorEntityID;

extern ManipulateTransformState manipulateState;
extern const c8** consoleLines;
// Application lifecycle hooks implemented for the Editor
void processInput(void* appData);
void init();
void update(f32 dt);
void render(f32 dt);
void destroy();

// Project build & game DLL management (implemented in editor.cpp)
void doBuildAndRun();
void doStopGame();
void resetRuntimeFreeViewState();

// Snap sceneCam to the first active Player entity (eye-height adjusted).
// Returns true when a Player entity was found.
b8 snapRuntimeFreeViewToPlayerEntity();

// Editsor UI drawing entry point (panels, dockspace, etc.)
void drawDockspaceAndPanels();

// Rebind the global field pointers after the archetype changes
void rebindArchetypeFields();

// Migrate a loaded scene archetype to the current SceneEntity layout if it
// has fewer fields (e.g. old scenes missing archetypeID). Safe to call even
// when no migration is needed.
void migrateSceneArchetypeIfNeeded();

// Scan the project src/ for archetype system files and populate the editor
// registry. Should be called once after a project is opened.
void scanProjectArchetypes(const c8 *projectDir);

// Stable-archetype mapping helpers:
// - hashes are persisted with the scene
// - IDs are rebuilt from hashes after registry changes/load
void syncSceneArchetypeHashesFromIDs();
void remapSceneArchetypeIDsFromHashes();

// Sync the editor camera from the designated scene-camera entity, if present.
void applySceneCameraEntityToSceneCam();

// Release material registry GPU resources (preview FBO, GBuffer, sphere mesh).
void shutdownMaterialRegistry();

// Build/reset the shader source-path tracking table from resDir.
// Call once after the initial readResources() on project open.
void buildShaderSourceTable(const c8 *resDir);

// Reload all project resources (new assets + force-recompile all shaders).
void reloadProjectResources();

// Load the project skybox if present, otherwise fall back to defaults.
void loadPreferredSkybox();

void editorLog(LogLevel level, const c8* msg);

// Multi-FBO system functions
void initMultiFBOs();
void destroyMultiFBOs();
void renderFBOToScreen(u32 fboIndex, u32 shaderProgram);
