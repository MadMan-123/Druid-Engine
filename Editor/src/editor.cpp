
// Define DRUID_H to enable archetype definitions in this compilation unit
#define DRUID_H

#include "editor.h"
#include "hub.h"
#include "project_builder.h"
#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_impl_opengl3.h"
#include "../deps/imgui/imgui_impl_sdl3.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../deps/imgui/imgui_internal.h"
#include "entitypicker.h"

#ifdef _WIN32
#pragma push_macro("ERROR")
#undef ERROR
#include <windows.h>
#pragma pop_macro("ERROR")
#else
#include <sys/stat.h>
#endif

// Some platform/third-party headers may define ERROR as a non-callable macro.
// Force this translation unit to use the engine logging API macro.
#ifdef ERROR
#undef ERROR
#endif
#define ERROR(message, ...) logOutput(LOG_ERROR, message, ##__VA_ARGS__)

// SceneEntity archetype definition — field order matches rebindArchetypeFields() expectations
#define SCENEENTITY_FIELDS(FIELD)                                                   \
    FIELD(SE_POS,           "position",          Vec3, HOT)                         \
    FIELD(SE_ROT,           "rotation",          Vec4, HOT)                         \
    FIELD(SE_SCALE,         "scale",             Vec3, HOT)                         \
    FIELD(SE_ACTIVE,        "isActive",          b8,   HOT)                         \
    FIELD(SE_NAME,          "name",              c8[MAX_NAME_SIZE], COLD)           \
    FIELD(SE_MODEL_ID,      "modelID",           u32,  COLD)                        \
    FIELD(SE_SHADER,        "shaderHandle",      u32,  COLD)                        \
    FIELD(SE_MATERIAL,      "materialID",        u32,  COLD)                        \
    FIELD(SE_ARCH_ID,       "archetypeID",       u32,  COLD)                        \
    FIELD(SE_SCENE_CAM,     "isSceneCamera",     b8,   COLD)                        \
    FIELD(SE_ECS_SLOT,      "ecsSlotID",         u32,  COLD)                        \
    FIELD(SE_TAG,           "tag",               c8[32], COLD)                      \
    FIELD(SE_PHYS_TYPE,     "physicsBodyType",   u32,  COLD)                        \
    FIELD(SE_MASS,          "mass",              f32,  COLD)                        \
    FIELD(SE_COLLIDER_SHAPE,"colliderShape",     u32,  COLD)                        \
    FIELD(SE_SPHERE_R,      "sphereRadius",      f32,  COLD)                        \
    FIELD(SE_HALF_X,        "colliderHalfX",     f32,  COLD)                        \
    FIELD(SE_HALF_Y,        "colliderHalfY",     f32,  COLD)                        \
    FIELD(SE_HALF_Z,        "colliderHalfZ",     f32,  COLD)                        \
    FIELD(SE_IS_LIGHT,      "isLight",           b8,   COLD)                        \
    FIELD(SE_LIGHT_TYPE,    "lightType",         u32,  COLD)                        \
    FIELD(SE_LIGHT_RANGE,   "lightRange",        f32,  COLD)                        \
    FIELD(SE_LIGHT_R,       "lightColorR",       f32,  COLD)                        \
    FIELD(SE_LIGHT_G,       "lightColorG",       f32,  COLD)                        \
    FIELD(SE_LIGHT_B,       "lightColorB",       f32,  COLD)                        \
    FIELD(SE_LIGHT_INT,     "lightIntensity",    f32,  COLD)                        \
    FIELD(SE_LIGHT_DIR_X,   "lightDirX",         f32,  COLD)                        \
    FIELD(SE_LIGHT_DIR_Y,   "lightDirY",         f32,  COLD)                        \
    FIELD(SE_LIGHT_DIR_Z,   "lightDirZ",         f32,  COLD)                        \
    FIELD(SE_LIGHT_IN_CONE, "lightInnerCone",    f32,  COLD)                        \
    FIELD(SE_LIGHT_OUT_CONE,"lightOuterCone",    f32,  COLD)

// Enum for field indices
enum { SCENEENTITY_FIELDS(_ARCH_ENUM_ENTRY) SceneEntity_FIELD_COUNT };

// Field metadata
FieldInfo SceneEntity_fields[] = { SCENEENTITY_FIELDS(_ARCH_FIELD_ENTRY) };
StructLayout SceneEntity = { "SceneEntity", SceneEntity_fields,
                             (u32)(sizeof(SceneEntity_fields) / sizeof(FieldInfo)) };

// Editor-side registry of user-created archetypes
static ArchetypeRegistry g_archRegistry = {0};

const c8 **consoleLines = NULL;
static u32 g_consoleCount = 0;

Application *editor = nullptr;

// UI state for scene menu modals
c8 scenePathBuffer[512] = "";
static b8 showSaveModal = false;
static b8 showLoadModal = false;
static b8 showNewSceneModal = false;
static b8 showProfiler = false;
static b8 showScenesPanel = true;
b8 showSkyboxSettings = false;
static b8 showCameraSettings = false;
b8 showColliders = true;
static b8 showLightGizmos = true;
static b8 showLoadedEntities = false;

// Editor camera clip/fov settings — adjusted via Camera Settings panel
static f32 g_editorFov  = 70.0f;
static f32 g_editorNear = 0.1f;
static f32 g_editorFar  = 1000.0f;

// Skybox face paths (6 faces: right, left, top, bottom, front, back)
static c8 g_skyboxFaces[6][512] = {
    "../res/Textures/Skybox/right.jpg",
    "../res/Textures/Skybox/left.jpg",
    "../res/Textures/Skybox/top.jpg",
    "../res/Textures/Skybox/bottom.jpg",
    "../res/Textures/Skybox/front.jpg",
    "../res/Textures/Skybox/back.jpg"
};

static const c8 *g_skyboxFaceSuffixes[6] = {
    "right.jpg", "left.jpg", "top.jpg",
    "bottom.jpg", "front.jpg", "back.jpg"
};

// Multi-FBO system
Framebuffer viewportFBs[MAX_FBOS] = {0};
Framebuffer finalDisplayFB = {0};  // Final processed result for ImGui
u32 viewportWidth = 0;
u32 viewportHeight = 0;
u32 activeFBO = 0;  // Which FBO to render to

// Screen quad for FBO rendering
Mesh *screenQuadMesh = nullptr;
Mesh *skyboxMesh = nullptr;
u32 cubeMapTexture = 0;
u32 skyboxShader = 0;
u32 skyboxViewLoc = 0;
u32 skyboxProjLoc = 0;

// Shared error string for skybox UI feedback
static char g_skyboxError[256] = "";

// Reload cubemap from 6 individual face texture paths
static void reloadSkyboxFromFaces(const c8 *facePaths[6])
{
    const c8 *faces[6];
    for (u32 i = 0; i < 6; i++)
        faces[i] = facePaths[i];

    // Validate that all 6 face files exist before attempting GPU upload
    b8 allValid = true;
    for (u32 i = 0; i < 6; i++)
    {
        if (!facePaths[i] || facePaths[i][0] == '\0')
        {
            ERROR("Skybox face %u: path is empty", i);
            allValid = false;
            continue;
        }
        if (!fileExists(facePaths[i]))
        {
            ERROR("Skybox face %u missing: %s", i, facePaths[i]);
            allValid = false;
        }
    }
    if (!allValid)
    {
        WARN("Cannot load skybox: one or more face images missing");
        snprintf(g_skyboxError, sizeof(g_skyboxError), "Failed to load skybox: one or more face files not found");
        return;
    }

    // Create the new cubemap FIRST, before destroying the old one
    u32 newTex = createCubeMapTexture(faces, 6);
    if (newTex == 0)
    {
        ERROR("Failed to load skybox cubemap");
        snprintf(g_skyboxError, sizeof(g_skyboxError), "Failed to create cubemap texture (check face image formats)");
        return;
    }

    // Only destroy the old texture after the new one succeeds
    if (cubeMapTexture != 0)
        freeTexture(cubeMapTexture);
    cubeMapTexture = newTex;

    // store the new paths
    for (u32 i = 0; i < 6; i++)
    {
        strncpy(g_skyboxFaces[i], facePaths[i], sizeof(g_skyboxFaces[i]) - 1);
        g_skyboxFaces[i][sizeof(g_skyboxFaces[i]) - 1] = '\0';
    }

    // Keep the renderer's env map in sync with the skybox cubemap
    if (renderer)
        renderer->envMapTex = cubeMapTexture;

    // Clear any previous error on success
    g_skyboxError[0] = '\0';
    INFO("Skybox reloaded");
}

static GizmoColor makeGizmoColor(f32 r, f32 g, f32 b, f32 a)
{
    GizmoColor c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

// Convenience: load skybox from a folder using standard naming
static void reloadSkyboxFromFolder(const c8 *folderPath)
{
    const c8 *suffixes[6] = {"right.jpg","left.jpg","top.jpg","bottom.jpg","front.jpg","back.jpg"};
    c8 paths[6][512];
    const c8 *ptrs[6];
    for (u32 i = 0; i < 6; i++)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", folderPath, suffixes[i]);
        ptrs[i] = paths[i];
    }
    reloadSkyboxFromFaces(ptrs);
}

f32 viewportWidthPixels = 0.0f;
f32 viewportHeightPixels = 0.0f;
f32 viewportOffsetX = 0.0f;
f32 viewportOffsetY = 0.0f;

Camera sceneCam = {0};
u32 g_editorCamSlot = (u32)-1; // renderer camera slot
b8 *sceneCameraFlags = NULL;
u32 entitySizeCache = 0;
Vec3 EulerAngles = v3Zero;

b8 manipulateTransform = false;

u32 entityCount = 0;
InspectorState currentInspectorState = EMPTY_VIEW;

u32 inspectorEntityID = 0;

u32 arrowShader = 0;
u32 colourLocation = 0;
u32 fboShader = 0;  // FBO post-processing shader
u32 deferredLightingShader = 0;

ManipulateTransformState manipulateState = MANIPULATE_POSITION;

// ---- Live ECS system plugins loaded from the game DLL ----
// During play mode, the editor auto-discovers ECS system entry points from
// the game DLL and calls their update/render callbacks each frame.
static ECSSystemPlugin g_ecsSystems[MAX_ARCHETYPE_SYSTEMS] = {0};
static b8              g_ecsSystemLoaded[MAX_ARCHETYPE_SYSTEMS] = {0};
static u32             g_ecsSystemCount = 0;

// ---- Per-archetype system data (created at play start, destroyed at stop) ----
// Each registered archetype gets its own Archetype instance populated only
// with the scene entities assigned to it. This way each ECS system operates
// on its own field layout and entity set.
static Archetype g_ecsArchetypes[MAX_ARCHETYPE_SYSTEMS] = {0};
static u32      *g_ecsSceneMap[MAX_ARCHETYPE_SYSTEMS]    = {0}; // per-sys idx → scene entity id
static u32       g_ecsArchEntityCount[MAX_ARCHETYPE_SYSTEMS] = {0};
static i32       g_ecsFieldMap[MAX_ARCHETYPE_SYSTEMS][64];     // [sys][field] → scene field idx (-1 = no match)
static i32       g_ecsUniformScaleIdx[MAX_ARCHETYPE_SYSTEMS];            // system field idx for f32 Scale (-1 = none)
static b8        g_archPhysRegistered[MAX_ARCHETYPE_SYSTEMS] = {0};      // cached physics registration state

// Cached collider field indices per archetype (avoid strcmp every frame)
struct ColliderFieldCache { i32 posX, posY, posZ, radius, halfX, halfY, halfZ; b8 resolved; };
static ColliderFieldCache g_colliderCache[MAX_ARCHETYPE_SYSTEMS] = {0};

// Scanned field storage — populated by scanProjectArchetypes so the registry
// has valid layout.fields pointers even before the DLL is loaded.
static FieldInfo g_scannedFields[MAX_ARCHETYPE_SYSTEMS][32];
static c8        g_scannedFieldNames[MAX_ARCHETYPE_SYSTEMS][32][128];

// Physics archetype for scene entities that aren't part of any ECS archetype
// but still need collision (e.g. static floor, walls).  Built at play-start
// from scene entities whose colliderShape != 0 and archetypeID == (u32)-1.
static FieldInfo g_scenePhysFields[] = {
    { "PositionX",       sizeof(f32), FIELD_TEMP_HOT },
    { "PositionY",       sizeof(f32), FIELD_TEMP_HOT },
    { "PositionZ",       sizeof(f32), FIELD_TEMP_HOT },
    { "LinearVelocityX", sizeof(f32), FIELD_TEMP_COLD },
    { "LinearVelocityY", sizeof(f32), FIELD_TEMP_COLD },
    { "LinearVelocityZ", sizeof(f32), FIELD_TEMP_COLD },
    { "ForceX",          sizeof(f32), FIELD_TEMP_COLD },
    { "ForceY",          sizeof(f32), FIELD_TEMP_COLD },
    { "ForceZ",          sizeof(f32), FIELD_TEMP_COLD },
    { "PhysicsBodyType", sizeof(u32), FIELD_TEMP_COLD },
    { "Mass",            sizeof(f32), FIELD_TEMP_COLD },
    { "InvMass",         sizeof(f32), FIELD_TEMP_COLD },
    { "Restitution",     sizeof(f32), FIELD_TEMP_COLD },
    { "LinearDamping",   sizeof(f32), FIELD_TEMP_COLD },
    { "SphereRadius",    sizeof(f32), FIELD_TEMP_COLD },
    { "ColliderHalfX",   sizeof(f32), FIELD_TEMP_COLD },
    { "ColliderHalfY",   sizeof(f32), FIELD_TEMP_COLD },
    { "ColliderHalfZ",   sizeof(f32), FIELD_TEMP_COLD },
    { "ColliderShape",   sizeof(u32), FIELD_TEMP_COLD },
    { "Scale",           sizeof(Vec3), FIELD_TEMP_COLD },
};
static StructLayout g_scenePhysLayout = {
    "ScenePhysics", g_scenePhysFields,
    sizeof(g_scenePhysFields) / sizeof(FieldInfo)
};
static Archetype g_scenePhysArch = {0};
static b8        g_scenePhysCreated = false;

// ---- Resource Manager state (hosts Materials, Textures, Shaders, Models tabs) ----
static b8          showResourceManager   = false;
static i32         g_resourceManagerTab  = 3;   // 0=Textures 1=Shaders 2=Models 3=Materials
static i32         g_selectedMaterialIdx = -1;
static c8          g_matNameFilter[128]  = "";

// Preview resources (lazily initialised on first use, destroyed in destroy())
static GBuffer     g_matPreviewGBuffer   = {0};
static Framebuffer g_matPreviewOutputFBO = {0};
static Mesh       *g_matPreviewSphere    = nullptr;
static b8          g_matPreviewReady     = false;
static const u32   MATPREVIEW_SIZE       = 256;

// Model preview resources
static GBuffer     g_modelPreviewGBuffer   = {0};
static Framebuffer g_modelPreviewOutputFBO = {0};
static b8          g_modelPreviewReady     = false;
static i32         g_selectedModelIdx      = -1;

// Render the selected material onto a sphere using the full deferred pipeline.
// Must be called after uploadSceneLights() has run (i.e. after drawViewportWindow).
static void renderMaterialPreview(u32 matIdx)
{
    if (!resources || matIdx >= resources->materialUsed) return;

    // Lazy init
    if (!g_matPreviewReady)
    {
        g_matPreviewGBuffer   = createGBuffer(MATPREVIEW_SIZE, MATPREVIEW_SIZE);
        g_matPreviewOutputFBO = createFramebuffer(MATPREVIEW_SIZE, MATPREVIEW_SIZE, GL_RGBA8, false);
        g_matPreviewSphere    = createSphereMesh();
        g_matPreviewReady = (g_matPreviewGBuffer.fbo != 0
                           && g_matPreviewOutputFBO.fbo != 0
                           && g_matPreviewSphere != nullptr);
        if (!g_matPreviewReady) return;
    }

    Material *mat = &resources->materialBuffer[matIdx];

    // Save GL state
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Override core UBO with a fixed preview camera
    Vec3 previewCamPos = {0.0f, 0.4f, 2.6f};
    Vec3 previewTarget = {0.0f, 0.0f, 0.0f};
    Vec3 up            = {0.0f, 1.0f, 0.0f};
    Mat4 previewView   = mat4LookAt(previewCamPos, previewTarget, up);
    Mat4 previewProj   = mat4Perspective(radians(45.0f), 1.0f, 0.1f, 20.0f);
    updateCoreShaderUBO(0.0f, &previewCamPos, &previewView, &previewProj);

    // --- G-Buffer pass ---
    glBindFramebuffer(GL_FRAMEBUFFER, g_matPreviewGBuffer.fbo);
    glViewport(0, 0, (GLsizei)MATPREVIEW_SIZE, (GLsizei)MATPREVIEW_SIZE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(shader);
    Mat4 identity = mat4Identity();
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, (f32 *)&identity);
    GLint baseLoc = glGetUniformLocation(shader, "u_modelBaseIndex");
    if (baseLoc >= 0) glUniform1i(baseLoc, -1);
    MaterialUniforms mu = getMaterialUniforms(shader);
    updateMaterial(mat, &mu);
    drawMesh(g_matPreviewSphere);

    // --- Lighting pass (identical to scene deferred pass) ---
    glBindFramebuffer(GL_FRAMEBUFFER, g_matPreviewOutputFBO.fbo);
    glViewport(0, 0, (GLsizei)MATPREVIEW_SIZE, (GLsizei)MATPREVIEW_SIZE);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(deferredLightingShader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_matPreviewGBuffer.positionTex);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "gPosition"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_matPreviewGBuffer.normalTex);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "gNormal"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_matPreviewGBuffer.albedoSpecTex);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "gAlbedoSpec"), 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "envMap"), 3);

    // Light SSBO (binding 3) was already populated by uploadSceneLights() this frame
    glBindVertexArray(screenQuadMesh->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);

    // Restore framebuffer and viewport (UBO restored by rendererBeginFrame next frame)
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void shutdownMaterialRegistry()
{
    if (g_matPreviewGBuffer.fbo)
    {
        glDeleteFramebuffers(1, &g_matPreviewGBuffer.fbo);
        if (g_matPreviewGBuffer.positionTex)  glDeleteTextures(1, &g_matPreviewGBuffer.positionTex);
        if (g_matPreviewGBuffer.normalTex)    glDeleteTextures(1, &g_matPreviewGBuffer.normalTex);
        if (g_matPreviewGBuffer.albedoSpecTex)glDeleteTextures(1, &g_matPreviewGBuffer.albedoSpecTex);
        if (g_matPreviewGBuffer.depthTex)     glDeleteTextures(1, &g_matPreviewGBuffer.depthTex);
        g_matPreviewGBuffer = {0};
    }
    if (g_matPreviewOutputFBO.fbo)
    {
        destroyFramebuffer(&g_matPreviewOutputFBO);
        g_matPreviewOutputFBO = {0};
    }
    if (g_matPreviewSphere)
    {
        freeMesh(g_matPreviewSphere);
        g_matPreviewSphere = nullptr;
    }
    g_matPreviewReady = false;

    if (g_modelPreviewGBuffer.fbo)
    {
        glDeleteFramebuffers(1, &g_modelPreviewGBuffer.fbo);
        if (g_modelPreviewGBuffer.positionTex)  glDeleteTextures(1, &g_modelPreviewGBuffer.positionTex);
        if (g_modelPreviewGBuffer.normalTex)    glDeleteTextures(1, &g_modelPreviewGBuffer.normalTex);
        if (g_modelPreviewGBuffer.albedoSpecTex)glDeleteTextures(1, &g_modelPreviewGBuffer.albedoSpecTex);
        if (g_modelPreviewGBuffer.depthTex)     glDeleteTextures(1, &g_modelPreviewGBuffer.depthTex);
        g_modelPreviewGBuffer = {0};
    }
    if (g_modelPreviewOutputFBO.fbo)
    {
        destroyFramebuffer(&g_modelPreviewOutputFBO);
        g_modelPreviewOutputFBO = {0};
    }
    g_modelPreviewReady = false;
}

static void renderModelPreview(u32 modelIdx)
{
    if (!resources || modelIdx >= resources->modelUsed) return;
    if (!shader || !deferredLightingShader || !screenQuadMesh) return;
    Model *model = &resources->modelBuffer[modelIdx];
    if (model->meshCount == 0) return;

    if (!g_modelPreviewReady)
    {
        g_modelPreviewGBuffer   = createGBuffer(MATPREVIEW_SIZE, MATPREVIEW_SIZE);
        g_modelPreviewOutputFBO = createFramebuffer(MATPREVIEW_SIZE, MATPREVIEW_SIZE, GL_RGBA8, false);
        g_modelPreviewReady = (g_modelPreviewGBuffer.fbo != 0 && g_modelPreviewOutputFBO.fbo != 0);
        if (!g_modelPreviewReady) return;
    }

    GLint prevFBO; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4]; glGetIntegerv(GL_VIEWPORT, prevViewport);

    f32 r    = (model->boundingRadius > 0.0f) ? model->boundingRadius : 1.0f;
    Vec3 camPos = {r * 1.2f, r * 0.6f, r * 2.0f};
    Vec3 target = {0.0f, 0.0f, 0.0f};
    Vec3 up     = {0.0f, 1.0f, 0.0f};
    Mat4 view   = mat4LookAt(camPos, target, up);
    Mat4 proj   = mat4Perspective(radians(45.0f), 1.0f, r * 0.01f, r * 20.0f);
    updateCoreShaderUBO(0.0f, &camPos, &view, &proj);

    // G-Buffer pass
    glBindFramebuffer(GL_FRAMEBUFFER, g_modelPreviewGBuffer.fbo);
    glViewport(0, 0, (GLsizei)MATPREVIEW_SIZE, (GLsizei)MATPREVIEW_SIZE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(shader);
    Mat4 modelMat = mat4Identity();
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, (f32 *)&modelMat);
    GLint baseLoc = glGetUniformLocation(shader, "u_modelBaseIndex");
    if (baseLoc >= 0) glUniform1i(baseLoc, -1);
    MaterialUniforms mu = getMaterialUniforms(shader);

    // Fallback material used when a mesh has no material assignment, so we always
    // call updateMaterial — preventing stale scene G-buffer textures from bleeding in.
    Material fallbackMat = {0};
    fallbackMat.colour   = {0.8f, 0.8f, 0.8f};
    fallbackMat.roughness = 0.5f;

    for (u32 m = 0; m < model->meshCount; m++)
    {
        u32 meshIdx = model->meshIndices[m];
        if (meshIdx >= resources->meshUsed) continue;
        Material *matToUse = &fallbackMat;
        if (model->materialIndices && m < model->materialCount)
        {
            u32 matIdx = model->materialIndices[m];
            if (matIdx < resources->materialUsed)
                matToUse = &resources->materialBuffer[matIdx];
        }
        updateMaterial(matToUse, &mu);
        drawMesh(&resources->meshBuffer[meshIdx]);
    }

    // Lighting pass
    glBindFramebuffer(GL_FRAMEBUFFER, g_modelPreviewOutputFBO.fbo);
    glViewport(0, 0, (GLsizei)MATPREVIEW_SIZE, (GLsizei)MATPREVIEW_SIZE);
    glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(deferredLightingShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_modelPreviewGBuffer.positionTex);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "gPosition"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_modelPreviewGBuffer.normalTex);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "gNormal"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_modelPreviewGBuffer.albedoSpecTex);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "gAlbedoSpec"), 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
    glUniform1i(glGetUniformLocation(deferredLightingShader, "envMap"), 3);

    glBindVertexArray(screenQuadMesh->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

// ---- Shader hot-reload ----

struct TrackedProjectShader {
    c8  name[128];
    c8  vertPath[512];
    c8  fragPath[512];
    u64 vertMtime;
    u64 fragMtime;
    b8  valid;
};
struct EditorTrackedShader {
    const c8 *vertPath;
    const c8 *fragPath;
    u32      *handlePtr;
    u64       vertMtime;
    u64       fragMtime;
};

static TrackedProjectShader g_trackedProjectShaders[128] = {};
static u32                  g_trackedProjectShaderCount  = 0;
static u64                  g_lastResCheckMs             = 0;
static EditorTrackedShader  g_editorShaders[] = {
    { "../res/shader.vert",            "../res/shader.frag",            &shader,                0, 0 },
    { "../res/arrow.vert",             "../res/arrow.frag",             &arrowShader,           0, 0 },
    { "../res/Skybox.vert",            "../res/Skybox.frag",            &skyboxShader,          0, 0 },
    { "../res/FBOShader.vert",         "../res/FBOShader.frag",         &fboShader,             0, 0 },
    { "../res/deferred_lighting.vert", "../res/deferred_lighting.frag", &deferredLightingShader,0, 0 },
    { "../res/idShader.vert",          "../res/idShader.frag",          &idShader,              0, 0 },
};
static const u32 g_editorShaderCount = (u32)(sizeof(g_editorShaders) / sizeof(g_editorShaders[0]));

static u64 getFileMtime(const c8 *path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return 0;
    ULARGE_INTEGER ul;
    ul.LowPart  = d.ftLastWriteTime.dwLowDateTime;
    ul.HighPart = d.ftLastWriteTime.dwHighDateTime;
    return (u64)ul.QuadPart;
#else
    struct stat st;
    return (stat(path, &st) == 0) ? (u64)st.st_mtime : 0;
#endif
}

// Populate project shader tracking table from scratch.
// Also snapshots current mtimes for editor shaders as the change-detection baseline.
// Call once after project resources are loaded (main.cpp) and after manual reload.
void buildShaderSourceTable(const c8 *resDir)
{
    // Baseline editor shader mtimes so we only recompile when files actually change
    for (u32 i = 0; i < g_editorShaderCount; i++) {
        g_editorShaders[i].vertMtime = getFileMtime(g_editorShaders[i].vertPath);
        g_editorShaders[i].fragMtime = getFileMtime(g_editorShaders[i].fragPath);
    }
    g_trackedProjectShaderCount = 0;
    if (!resources || !resDir || resDir[0] == '\0') return;
    HashMap *map = &resources->shaderIDs;
    for (u32 i = 0; i < map->capacity && g_trackedProjectShaderCount < 128; i++) {
        if (!map->pairs[i].occupied) continue;
        const c8 *name = (const c8 *)map->pairs[i].key;
        c8 vertPath[512], fragPath[512];
        snprintf(vertPath, sizeof(vertPath), "%s%s.vert", resDir, name);
        snprintf(fragPath, sizeof(fragPath), "%s%s.frag", resDir, name);
        if (!fileExists(vertPath) || !fileExists(fragPath)) continue;
        TrackedProjectShader &t = g_trackedProjectShaders[g_trackedProjectShaderCount++];
        strncpy(t.name,     name,     sizeof(t.name)     - 1); t.name[sizeof(t.name)-1]         = '\0';
        strncpy(t.vertPath, vertPath, sizeof(t.vertPath) - 1); t.vertPath[sizeof(t.vertPath)-1] = '\0';
        strncpy(t.fragPath, fragPath, sizeof(t.fragPath) - 1); t.fragPath[sizeof(t.fragPath)-1] = '\0';
        t.vertMtime = getFileMtime(vertPath);
        t.fragMtime = getFileMtime(fragPath);
        t.valid = true;
    }
}

// Merge new project shaders into the tracking table without resetting existing entries.
static void mergeShaderSourceTable(const c8 *resDir)
{
    if (!resources || !resDir || resDir[0] == '\0') return;
    HashMap *map = &resources->shaderIDs;
    for (u32 i = 0; i < map->capacity && g_trackedProjectShaderCount < 128; i++) {
        if (!map->pairs[i].occupied) continue;
        const c8 *name = (const c8 *)map->pairs[i].key;
        b8 found = false;
        for (u32 j = 0; j < g_trackedProjectShaderCount; j++)
            if (strcmp(g_trackedProjectShaders[j].name, name) == 0) { found = true; break; }
        if (found) continue;
        c8 vertPath[512], fragPath[512];
        snprintf(vertPath, sizeof(vertPath), "%s%s.vert", resDir, name);
        snprintf(fragPath, sizeof(fragPath), "%s%s.frag", resDir, name);
        if (!fileExists(vertPath) || !fileExists(fragPath)) continue;
        TrackedProjectShader &t = g_trackedProjectShaders[g_trackedProjectShaderCount++];
        strncpy(t.name,     name,     sizeof(t.name)     - 1); t.name[sizeof(t.name)-1]         = '\0';
        strncpy(t.vertPath, vertPath, sizeof(t.vertPath) - 1); t.vertPath[sizeof(t.vertPath)-1] = '\0';
        strncpy(t.fragPath, fragPath, sizeof(t.fragPath) - 1); t.fragPath[sizeof(t.fragPath)-1] = '\0';
        t.vertMtime = getFileMtime(vertPath);
        t.fragMtime = getFileMtime(fragPath);
        t.valid = true;
    }
}

// Recompile all tracked shaders whose source mtimes have changed.
static void recompileChangedShaders()
{
    // Editor shaders
    for (u32 i = 0; i < g_editorShaderCount; i++) {
        EditorTrackedShader &e = g_editorShaders[i];
        u64 newVert = getFileMtime(e.vertPath);
        u64 newFrag = getFileMtime(e.fragPath);
        if (newVert == e.vertMtime && newFrag == e.fragMtime) continue;
        u32 oldProg = *e.handlePtr;
        u32 newProg = createGraphicsProgram(e.vertPath, e.fragPath);
        if (newProg == 0) { ERROR("Hot-reload: failed to recompile %s", e.vertPath); continue; }
        if (shaderHandles && oldProg != 0)
            for (u32 j = 0; j < entityCount; j++)
                if (shaderHandles[j] == oldProg) shaderHandles[j] = newProg;
        freeShader(oldProg);
        *e.handlePtr = newProg;
        e.vertMtime  = newVert;
        e.fragMtime  = newFrag;
        INFO("Shader recompiled: %s", e.vertPath);
        if (e.handlePtr == &shader) {
            materialUniforms = getMaterialUniforms(shader);
        } else if (e.handlePtr == &arrowShader) {
            colourLocation = glGetUniformLocation(arrowShader, "colour");
        } else if (e.handlePtr == &skyboxShader) {
            skyboxViewLoc = glGetUniformLocation(skyboxShader, "view");
            skyboxProjLoc = glGetUniformLocation(skyboxShader, "projection");
        } else if (e.handlePtr == &idShader) {
            idLocation = glGetUniformLocation(idShader, "entityID");
        }
    }
    // Project shaders
    for (u32 i = 0; i < g_trackedProjectShaderCount; i++) {
        TrackedProjectShader &t = g_trackedProjectShaders[i];
        if (!t.valid) continue;
        u64 newVert = getFileMtime(t.vertPath);
        u64 newFrag = getFileMtime(t.fragPath);
        if (newVert == t.vertMtime && newFrag == t.fragMtime) continue;
        u32 idx = 0;
        if (!findInMap(&resources->shaderIDs, t.name, &idx)) continue;
        u32 oldProg = resources->shaderHandles[idx];
        u32 newProg = createGraphicsProgram(t.vertPath, t.fragPath);
        if (newProg == 0) { ERROR("Hot-reload: failed to recompile project shader %s", t.name); continue; }
        if (shaderHandles && oldProg != 0)
            for (u32 j = 0; j < entityCount; j++)
                if (shaderHandles[j] == oldProg) shaderHandles[j] = newProg;
        freeShader(oldProg);
        resources->shaderHandles[idx] = newProg;
        t.vertMtime = newVert;
        t.fragMtime = newFrag;
        INFO("Project shader recompiled: %s", t.name);
    }
}

// Manual full reload: pick up new assets, merge new shader tracking entries,
// then force-recompile all tracked shaders regardless of mtime.
void reloadProjectResources()
{
    if (!hubProjectDir[0] || !resources) return;
    c8 projRes[MAX_PATH_LENGTH];
    snprintf(projRes, sizeof(projRes), "%s/res/", hubProjectDir);
    readResources(resources, projRes);
    mergeShaderSourceTable(projRes);
    // Force all tracked shaders to recompile by zeroing mtimes
    for (u32 i = 0; i < g_editorShaderCount; i++)
        g_editorShaders[i].vertMtime = g_editorShaders[i].fragMtime = 0;
    for (u32 i = 0; i < g_trackedProjectShaderCount; i++)
        g_trackedProjectShaders[i].vertMtime = g_trackedProjectShaders[i].fragMtime = 0;
    recompileChangedShaders();
    INFO("Project resources reloaded");
}

// Poll for file changes every 2 seconds; recompile changed shaders automatically.
static void checkForResourceChanges()
{
    if (!hubProjectDir[0] || !resources) return;
    u64 now = (u64)SDL_GetTicks();
    if (now - g_lastResCheckMs < 2000) return;
    g_lastResCheckMs = now;

    c8 projRes[MAX_PATH_LENGTH];
    snprintf(projRes, sizeof(projRes), "%s/res/", hubProjectDir);
    readResources(resources, projRes);
    mergeShaderSourceTable(projRes);
    recompileChangedShaders();
}

// ---- helpers ----

// Case-insensitive string comparison for matching ECS fields to scene fields.
static int fieldNameCmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// Look up a known type size by name (mirrors the Archetype Designer table).
static u32 knownTypeSize(const char *typeName)
{
    struct { const char *n; u32 s; } tbl[] = {
        {"f32",4},{"i32",4},{"u32",4},{"b8",1},{"Vec2",8},
        {"Vec3",12},{"Vec4",16},{"Mat4",64},{"u64",8},{"c8[256]",256}
    };
    for (u32 i = 0; i < sizeof(tbl)/sizeof(tbl[0]); i++)
        if (strcmp(typeName, tbl[i].n) == 0) return tbl[i].s;
    return 0;
}

// Populate per-system Archetype instances from the scene archetype.

static i32 findSceneCameraEntity()
{
    if (!sceneCameraFlags)
        return -1;

    for (u32 i = 0; i < entityCount; i++)
    {
        if (isActive && isActive[i] && sceneCameraFlags[i])
            return (i32)i;
    }
    return -1;
}

static void setSceneCameraEntity(u32 entityID)
{
    if (!sceneCameraFlags)
        return;

    for (u32 i = 0; i < entityCount; i++)
        sceneCameraFlags[i] = (i == entityID);
}

void applySceneCameraEntityToSceneCam()
{
    i32 camEntity = findSceneCameraEntity();
    if (camEntity < 0)
        return;

    sceneCam.pos = positions[camEntity];
    sceneCam.orientation = rotations[camEntity];
}

static b8 projectSkyboxExists(c8 *outFolder, u32 folderSize)
{
    if (!hubProjectDir[0])
        return false;

    c8 folder[512];
    snprintf(folder, sizeof(folder), "%s/res/Textures/Skybox", hubProjectDir);

    for (u32 i = 0; i < 6; i++)
    {
        c8 facePath[512];
        snprintf(facePath, sizeof(facePath), "%s/%s", folder, g_skyboxFaceSuffixes[i]);
        if (!fileExists(facePath))
            return false;
    }

    if (outFolder && folderSize > 0)
    {
        strncpy(outFolder, folder, folderSize - 1);
        outFolder[folderSize - 1] = '\0';
    }
    return true;
}

static void saveSkyboxFacesToProject(const c8 *facePaths[6])
{
    if (!hubProjectDir[0])
    {
        WARN("No project open - cannot persist skybox");
        return;
    }

    c8 folder[512];
    snprintf(folder, sizeof(folder), "%s/res/Textures/Skybox", hubProjectDir);
    createDir(folder);

    u32 copied = 0;
    for (u32 i = 0; i < 6; i++)
    {
        if (!facePaths[i] || facePaths[i][0] == '\0')
            continue;

        c8 dst[512];
        snprintf(dst, sizeof(dst), "%s/%s", folder, g_skyboxFaceSuffixes[i]);
        if (platformFileCopy(facePaths[i], dst))
            copied++;
        else
            WARN("Failed to copy skybox face %u to %s", i, dst);
    }

    if (copied == 6)
    {
        reloadSkyboxFromFolder(folder);
        INFO("Skybox saved to project resources");
    }
    else
    {
        WARN("Skybox saved partially (%u/6 faces)", copied);
    }
}

void loadPreferredSkybox()
{
    c8 folder[512];
    if (projectSkyboxExists(folder, sizeof(folder)))
    {
        reloadSkyboxFromFolder(folder);
        return;
    }

    const c8 *faces[6];
    for (u32 i = 0; i < 6; i++)
        faces[i] = g_skyboxFaces[i];
    reloadSkyboxFromFaces(faces);
}

static void drawSceneCameraMarker(const Transform &t)
{
    glUseProgram(arrowShader);
    updateShaderModel(arrowShader, t);
    glUniform3f(colourLocation, 1.0f, 0.85f, 0.2f);
    drawMesh(cubeMesh);
}
// Call after ECS system discovery in doBuildAndRun().
static void populateEcsArchetypes()
{
    // Clean up any leftovers
    for (u32 a = 0; a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        if (g_ecsArchetypes[a].arena) destroyArchetype(&g_ecsArchetypes[a]);
        memset(&g_ecsArchetypes[a], 0, sizeof(Archetype));
        free(g_ecsSceneMap[a]);
        g_ecsSceneMap[a] = nullptr;
        g_ecsArchEntityCount[a] = 0;
        memset(g_ecsFieldMap[a], -1, sizeof(g_ecsFieldMap[a]));
        g_ecsUniformScaleIdx[a] = -1;
    }

    void **scnFields = getArchetypeFields(&sceneArchetype, 0);
    if (!scnFields) return;

    for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        if (!g_ecsSystemLoaded[a]) continue;

        // Look up the StructLayout compiled into the game DLL.
        c8 symName[256];
        snprintf(symName, sizeof(symName), "%s_layout", g_archRegistry.entries[a].name);
        StructLayout *dllLayout = (StructLayout *)dllSymbol(&g_gameDLL.dll, symName);
        if (!dllLayout)
        {
            WARN("Could not find '%s' in DLL — skipping ECS archetype", symName);
            continue;
        }

        // Count scene entities assigned to this archetype
        u32 matchCount = 0;
        for (u32 id = 0; id < entityCount; id++)
            if (archetypeIDs && archetypeIDs[id] == a) matchCount++;

        // Always create the archetype even if no scene entities are assigned
        // (e.g. Bullet — entities are spawned at runtime via createEntityInArchetype).
        // Use a minimum capacity of 256 for dynamic archetypes so spawning works.
        u32 archCapacity = matchCount > 0 ? matchCount : 256;

        // Create per-system archetype using the DLL layout
        Archetype *sysArch = &g_ecsArchetypes[a];
        sysArch->flags         = g_archRegistry.entries[a].flags;
        sysArch->poolCapacity  = g_archRegistry.entries[a].poolCapacity;
        if (!createArchetype(dllLayout, archCapacity, sysArch))
        {
            ERROR("Failed to create per-system archetype for '%s'", g_archRegistry.entries[a].name);
            continue;
        }

        g_ecsSceneMap[a] = matchCount > 0 ? (u32 *)calloc(matchCount, sizeof(u32)) : nullptr;

        // Build field mapping: for each system field, find a scene field with
        // the same name (case-insensitive) AND the same byte size.
        // Special case: f32 Scale <-> Vec3 scale (uniform scale).
        g_ecsUniformScaleIdx[a] = -1;
        u32 mappedCount = 0;
        for (u32 f = 0; f < dllLayout->count && f < 64; f++)
        {
            g_ecsFieldMap[a][f] = -1;
            for (u32 sf = 0; sf < sceneArchetype.layout->count; sf++)
            {
                if (fieldNameCmp(dllLayout->fields[f].name,
                                 sceneArchetype.layout->fields[sf].name) == 0)
                {
                    if (dllLayout->fields[f].size == sceneArchetype.layout->fields[sf].size)
                    {
                        g_ecsFieldMap[a][f] = (i32)sf;
                        mappedCount++;
                        break;
                    }
                    // f32 Scale in system vs Vec3 scale in scene
                    if (dllLayout->fields[f].size == sizeof(f32)
                        && sceneArchetype.layout->fields[sf].size == sizeof(Vec3)
                        && fieldNameCmp(dllLayout->fields[f].name, "scale") == 0)
                    {
                        g_ecsUniformScaleIdx[a] = (i32)f;
                        // Don't put it in g_ecsFieldMap — handled separately
                        mappedCount++;
                        break;
                    }
                }
            }
        }

        // All archetypes now use SoA positions (PositionX/Y/Z as separate f32 fields).
        // Find these indices so we can decompose the scene Vec3 position into them.
        // Also find InvMass for physics bodies (computed from mass at play-start).
        i32 posXIdx = -1, posYIdx = -1, posZIdx = -1;
        i32 invMassIdx = -1;
        for (u32 f = 0; f < dllLayout->count; f++)
        {
            const c8 *n = dllLayout->fields[f].name;
            if      (strcmp(n, "PositionX") == 0) posXIdx    = (i32)f;
            else if (strcmp(n, "PositionY") == 0) posYIdx    = (i32)f;
            else if (strcmp(n, "PositionZ") == 0) posZIdx    = (i32)f;
            else if (strcmp(n, "InvMass")   == 0) invMassIdx = (i32)f;
        }

        // Populate the per-system archetype with matching scene entities
        void **sysFields = getArchetypeFields(sysArch, 0);
        u32 idx = 0;
        for (u32 id = 0; id < entityCount; id++)
        {
            if (!archetypeIDs || archetypeIDs[id] != a) continue;
            if (g_ecsSceneMap[a]) g_ecsSceneMap[a][idx] = id;

            // Record the ECS slot index back into the scene entity so game code
            // can look up its own position in the ECS SoA arrays.
            if (ecsSlotIDs) ecsSlotIDs[id] = idx;

            // Copy matching fields from scene → per-system
            for (u32 f = 0; f < dllLayout->count && f < 64; f++)
            {
                i32 sf = g_ecsFieldMap[a][f];
                if (sf < 0) continue;
                u32 sz = dllLayout->fields[f].size;
                memcpy((u8 *)sysFields[f] + sz * idx,
                       (u8 *)scnFields[sf] + sz * id, sz);
            }
            // Uniform scale: scene Vec3 → system f32 (take x component)
            if (g_ecsUniformScaleIdx[a] >= 0)
            {
                i32 sF = g_ecsUniformScaleIdx[a];
                Vec3 *scnScale = (Vec3 *)((u8 *)scnFields[2] + sizeof(Vec3) * id);
                f32  *sysScale = (f32  *)((u8 *)sysFields[sF] + sizeof(f32) * idx);
                *sysScale = scnScale->x;
            }
            // Decompose scene Vec3 position → SoA PositionX/Y/Z (all archetypes).
            if (posXIdx >= 0 && posYIdx >= 0 && posZIdx >= 0)
            {
                Vec3 pos = positions[id];
                ((f32 *)sysFields[posXIdx])[idx] = pos.x;
                ((f32 *)sysFields[posYIdx])[idx] = pos.y;
                ((f32 *)sysFields[posZIdx])[idx] = pos.z;
            }
            // Derive InvMass from the scene mass field (physics bodies only).
            if (invMassIdx >= 0 && masses)
            {
                f32 m = masses[id];
                ((f32 *)sysFields[invMassIdx])[idx] = (m > 0.0001f) ? (1.0f / m) : 0.0f;
            }

            sysArch->arena[0].count++;
            idx++;
        }
        g_ecsArchEntityCount[a] = matchCount;

        // Set activeChunkCount so save/iteration sees the entities
        if (matchCount > 0 && sysArch->activeChunkCount == 0)
            sysArch->activeChunkCount = 1;

        // For physics archetypes, default Restitution and LinearDamping since
        // they have no scene entity counterpart (all other physics fields are
        // now populated from scene entity fields via name matching).
        if (FLAG_CHECK(g_archRegistry.entries[a].flags, ARCH_PHYSICS_BODY) && matchCount > 0)
        {
            for (u32 f = 0; f < dllLayout->count; f++)
            {
                if (g_ecsFieldMap[a][f] >= 0) continue;  // already mapped from scene
                const c8 *n = dllLayout->fields[f].name;
                if (!n) continue;
                if (strcmp(n, "Restitution") == 0 && dllLayout->fields[f].size == sizeof(f32))
                {
                    f32 *arr = (f32 *)sysFields[f];
                    for (u32 e = 0; e < matchCount; e++) if (arr[e] == 0.0f) arr[e] = 0.3f;
                }
                else if (strcmp(n, "LinearDamping") == 0 && dllLayout->fields[f].size == sizeof(f32))
                {
                    f32 *arr = (f32 *)sysFields[f];
                    for (u32 e = 0; e < matchCount; e++) if (arr[e] == 0.0f) arr[e] = 0.01f;
                }
            }
        }

        INFO("ECS archetype '%s': %u entities (%u capacity), %u/%u fields mapped to scene",
             g_archRegistry.entries[a].name, matchCount, archCapacity, mappedCount, dllLayout->count);
    }
}

// Destroy all per-system archetypes. Call from doStopGame().
static void cleanupEcsArchetypes()
{
    for (u32 a = 0; a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        if (g_ecsArchetypes[a].arena) destroyArchetype(&g_ecsArchetypes[a]);
        memset(&g_ecsArchetypes[a], 0, sizeof(Archetype));
        free(g_ecsSceneMap[a]);
        g_ecsSceneMap[a] = nullptr;
        g_ecsArchEntityCount[a] = 0;
        g_ecsUniformScaleIdx[a] = -1;
    }
}

// Build a physics-only archetype for scene entities that have colliders but
// are not part of any ECS archetype (e.g. static floor, walls, obstacles).
// These entities need to exist in the physics broadphase so dynamic bodies
// (like the Player) can collide with them.
static void setupScenePhysicsArchetype()
{
    if (g_scenePhysCreated) return;

    void **scnFields = getArchetypeFields(&sceneArchetype, 0);
    if (!scnFields) return;
    u32 total = sceneArchetype.arena[0].count;

    Vec3 *positions      = (Vec3 *)scnFields[0];
    Vec3 *scales         = (Vec3 *)scnFields[2];
    b8   *isActive       = (b8   *)scnFields[3];
    u32  *archetypeIDs   = (u32  *)scnFields[8];
    u32  *physBodyTypes  = (u32  *)scnFields[12];
    f32  *masses         = (f32  *)scnFields[13];
    u32  *colliderShapes = (u32  *)scnFields[14];
    f32  *sphereRadii    = (f32  *)scnFields[15];
    f32  *halfXs         = (f32  *)scnFields[16];
    f32  *halfYs         = (f32  *)scnFields[17];
    f32  *halfZs         = (f32  *)scnFields[18];

    // Count eligible scene entities: active, has a collider or physics body type,
    // and either not assigned to an ECS archetype or assigned to one that has no
    // physics fields (e.g. built-in StaticObject which only has position/scale).
    u32 physCount = 0;
    for (u32 i = 0; i < total; i++)
    {
        if (!isActive[i]) continue;
        // Skip entities whose ECS archetype is already registered with physics
        // and has proper physics fields (velocity, mass, collider).
        u32 aid = archetypeIDs[i];
        if (aid != (u32)-1 && aid < MAX_ARCHETYPE_SYSTEMS && g_ecsArchetypes[aid].arena)
        {
            // Check if this archetype actually has physics collision fields
            StructLayout *lay = g_ecsArchetypes[aid].layout;
            b8 hasPhysFields = false;
            if (lay)
            {
                for (u32 f = 0; f < lay->count; f++)
                {
                    if (strcmp(lay->fields[f].name, "ColliderHalfX") == 0 ||
                        strcmp(lay->fields[f].name, "SphereRadius") == 0)
                    { hasPhysFields = true; break; }
                }
            }
            if (hasPhysFields) continue;  // archetype handles its own physics
        }
        b8 hasCollider = colliderShapes[i] != 0;
        b8 hasHalfExtents = halfXs[i] > 0.0f || halfYs[i] > 0.0f || halfZs[i] > 0.0f;
        b8 hasRadius = sphereRadii[i] > 0.0f;
        if (!hasCollider && !hasHalfExtents && !hasRadius) continue;
        physCount++;
    }

    INFO("setupScenePhysicsArchetype: %u scene entities, %u eligible for physics", total, physCount);
    if (physCount == 0) { INFO("  No scene physics entities found — skipping"); return; }

    g_scenePhysArch.flags = 0;
    FLAG_SET(g_scenePhysArch.flags, ARCH_PHYSICS_BODY);
    if (!createArchetype(&g_scenePhysLayout, physCount, &g_scenePhysArch))
    { ERROR("Failed to create scene physics archetype"); return; }
    g_scenePhysCreated = true;

    void **pf = getArchetypeFields(&g_scenePhysArch, 0);
    if (!pf) return;

    // ScenePhysics field indices (must match g_scenePhysFields order):
    f32  *spPosX   = (f32  *)pf[0];
    f32  *spPosY   = (f32  *)pf[1];
    f32  *spPosZ   = (f32  *)pf[2];
    // 3-5: LinearVelocityX/Y/Z (zeroed by default)
    // 6-8: ForceX/Y/Z (zeroed by default)
    u32  *spBT     = (u32  *)pf[9];
    f32  *spMass   = (f32  *)pf[10];
    f32  *spInvM   = (f32  *)pf[11];
    f32  *spRest   = (f32  *)pf[12];
    f32  *spDamp   = (f32  *)pf[13];
    f32  *spRad    = (f32  *)pf[14];
    f32  *spHX     = (f32  *)pf[15];
    f32  *spHY     = (f32  *)pf[16];
    f32  *spHZ     = (f32  *)pf[17];
    u32  *spShape  = (u32  *)pf[18];
    Vec3 *spScale  = (Vec3 *)pf[19];

    u32 idx = 0;
    for (u32 i = 0; i < total; i++)
    {
        if (!isActive[i]) continue;
        // Same eligibility check as counting pass above
        u32 aid = archetypeIDs[i];
        if (aid != (u32)-1 && aid < MAX_ARCHETYPE_SYSTEMS && g_ecsArchetypes[aid].arena)
        {
            StructLayout *lay = g_ecsArchetypes[aid].layout;
            b8 hasPhysFields = false;
            if (lay)
            {
                for (u32 f = 0; f < lay->count; f++)
                {
                    if (strcmp(lay->fields[f].name, "ColliderHalfX") == 0 ||
                        strcmp(lay->fields[f].name, "SphereRadius") == 0)
                    { hasPhysFields = true; break; }
                }
            }
            if (hasPhysFields) continue;
        }
        b8 hasCollider = colliderShapes[i] != 0;
        b8 hasHalfExtents = halfXs[i] > 0.0f || halfYs[i] > 0.0f || halfZs[i] > 0.0f;
        b8 hasRadius = sphereRadii[i] > 0.0f;
        if (!hasCollider && !hasHalfExtents && !hasRadius) continue;

        spPosX[idx]  = positions[i].x;
        spPosY[idx]  = positions[i].y;
        spPosZ[idx]  = positions[i].z;
        spBT[idx]    = physBodyTypes[i];
        spMass[idx]  = masses[i];
        spInvM[idx]  = (masses[i] > 0.0001f) ? (1.0f / masses[i]) : 0.0f;
        spRest[idx]  = 0.3f;
        spDamp[idx]  = 0.01f;
        spRad[idx]   = sphereRadii[i];
        spScale[idx] = scales[i];

        // Infer box collider from half-extents or scale if colliderShape not set
        u32 shape = colliderShapes[i];
        f32 hx = halfXs[i], hy = halfYs[i], hz = halfZs[i];
        if (shape == 0 && (hx > 0.0f || hy > 0.0f || hz > 0.0f))
            shape = 2; // Box
        if (shape == 0 && hasRadius)
            shape = 1; // Sphere
        if (shape == 2 && hx == 0.0f && hy == 0.0f && hz == 0.0f)
        {
            // Use half the scale as box extents
            hx = scales[i].x * 0.5f;
            hy = scales[i].y * 0.5f;
            hz = scales[i].z * 0.5f;
        }
        spHX[idx]    = hx;
        spHY[idx]    = hy;
        spHZ[idx]    = hz;
        spShape[idx] = shape;
        idx++;
    }
    g_scenePhysArch.arena[0].count = idx;
    if (idx > 0) g_scenePhysArch.activeChunkCount = 1;

    INFO("Scene physics: %u static colliders registered", idx);
}

static void cleanupScenePhysicsArchetype()
{
    if (g_scenePhysCreated)
    {
        destroyArchetype(&g_scenePhysArch);
        memset(&g_scenePhysArch, 0, sizeof(Archetype));
        g_scenePhysCreated = false;
    }
}

// Sync per-system archetype data back to the scene archetype so the editor
// renderer draws entities at their ECS-updated positions / rotations.
static void syncEcsToScene()
{
    void **scnFields = getArchetypeFields(&sceneArchetype, 0);
    if (!scnFields) return;

    for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        if (!g_ecsSystemLoaded[a] || g_ecsArchEntityCount[a] == 0) continue;

        void **sysFields = getArchetypeFields(&g_ecsArchetypes[a], 0);
        if (!sysFields) continue;

        StructLayout *sysLayout = g_ecsArchetypes[a].layout;

        // Cache PositionX/Y/Z field indices. These are f32 SoA fields that
        // don't match the scene Vec3 position by name+size, so the generic
        // field-copy loop below skips them — handle them explicitly here.
        i32 pxI = -1, pyI = -1, pzI = -1;
        for (u32 f = 0; f < sysLayout->count; f++)
        {
            const c8 *n = sysLayout->fields[f].name;
            if      (strcmp(n, "PositionX") == 0) pxI = (i32)f;
            else if (strcmp(n, "PositionY") == 0) pyI = (i32)f;
            else if (strcmp(n, "PositionZ") == 0) pzI = (i32)f;
        }

        for (u32 idx = 0; idx < g_ecsArchEntityCount[a]; idx++)
        {
            u32 sceneID = g_ecsSceneMap[a][idx];
            for (u32 f = 0; f < sysLayout->count && f < 64; f++)
            {
                i32 sf = g_ecsFieldMap[a][f];
                if (sf < 0) continue;
                u32 sz = sysLayout->fields[f].size;
                memcpy((u8 *)scnFields[sf] + sz * sceneID,
                       (u8 *)sysFields[f]  + sz * idx, sz);
            }
            // Uniform scale: system f32 → scene Vec3 (broadcast to all 3)
            if (g_ecsUniformScaleIdx[a] >= 0)
            {
                i32 sF = g_ecsUniformScaleIdx[a];
                f32  *sysScale = (f32  *)((u8 *)sysFields[sF] + sizeof(f32) * idx);
                Vec3 *scnScale = (Vec3 *)((u8 *)scnFields[2]  + sizeof(Vec3) * sceneID);
                scnScale->x = *sysScale;
                scnScale->y = *sysScale;
                scnScale->z = *sysScale;
            }
            // Physics readback: PositionX/Y/Z (f32 SoA) → scene position (Vec3)
            if (pxI >= 0 && pyI >= 0 && pzI >= 0)
            {
                Vec3 *scnPos = (Vec3 *)scnFields[0] + sceneID;
                scnPos->x = ((f32 *)sysFields[pxI])[idx];
                scnPos->y = ((f32 *)sysFields[pyI])[idx];
                scnPos->z = ((f32 *)sysFields[pzI])[idx];
            }
        }
    }
}

static void drawTextureSelector(const c8 *label, u32 *textureHandle, const c8 *comboID)
{
    if (!textureHandle || !resources) return;
    ImGui::Text("%s", label);
    ImGui::Image((void *)(intptr_t)(*textureHandle), ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
    ImGui::SameLine();
    if (ImGui::BeginCombo(comboID, "Select Texture"))
    {
        for (u32 texIdx = 0; texIdx < resources->textureIDs.capacity; texIdx++)
        {
            if (resources->textureIDs.pairs[texIdx].occupied)
            {
                const c8 *texName = (const c8 *)resources->textureIDs.pairs[texIdx].key;
                u32 textureIndex = *(u32 *)resources->textureIDs.pairs[texIdx].value;
                u32 handle = resources->textureHandles[textureIndex];

                const b8 is_selected = (*textureHandle == handle);
                if (ImGui::Selectable(texName, is_selected))
                {
                    *textureHandle = handle;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

static void resizeViewportFramebuffers(u32 width, u32 height)
{
    if (width <= 0 || height <= 0)
        return; // Ignore invalid sizes
    if (width == viewportWidth && height == viewportHeight) // No resize needed
        return;

    viewportWidth = width;
    viewportHeight = height;

    // Resize all FBOs
    for (u32 i = 0; i < MAX_FBOS; i++)
    {
        if (viewportFBs[i].fbo == 0)
        {
            viewportFBs[i] = createFramebuffer(viewportWidth, viewportHeight, GL_RGBA8, true);
        }
        else
        {
            resizeFramebuffer(&viewportFBs[i], viewportWidth, viewportHeight);
        }
    }
    
    // Resize GBuffer to match the viewport so the depth blit dimensions match
    if (renderer && renderer->useDeferredRendering)
    {
        rendererEnableDeferred(renderer, viewportWidth, viewportHeight);
    }

    // Resize final display FBO
    if (finalDisplayFB.fbo == 0)
    {
        finalDisplayFB = createFramebuffer(viewportWidth, viewportHeight, GL_RGBA8, false);
    }
    else
    {
        resizeFramebuffer(&finalDisplayFB, viewportWidth, viewportHeight);
    }
}

static void uploadSceneLights()
{
    if (!isLight || !positions) return;

    GPULight lights[MAX_LIGHTS];
    u32 count = 0;
    u32 total = sceneArchetype.arena[0].count;

    for (u32 i = 0; i < total && count < MAX_LIGHTS; i++)
    {
        if (!isLight[i]) continue;

        GPULight *l = &lights[count];
        l->posX = positions[i].x;
        l->posY = positions[i].y;
        l->posZ = positions[i].z;
        l->range     = lightRanges      ? lightRanges[i]      : 10.0f;
        l->colorR    = lightColorRs     ? lightColorRs[i]     : 1.0f;
        l->colorG    = lightColorGs     ? lightColorGs[i]     : 1.0f;
        l->colorB    = lightColorBs     ? lightColorBs[i]     : 1.0f;
        l->intensity = lightIntensities ? lightIntensities[i] : 1.0f;
        l->dirX      = lightDirXs       ? lightDirXs[i]       : 0.0f;
        l->dirY      = lightDirYs       ? lightDirYs[i]       : -1.0f;
        l->dirZ      = lightDirZs       ? lightDirZs[i]       : 0.0f;
        l->innerCone = lightInnerCones  ? lightInnerCones[i]  : 0.9063f;
        l->outerCone = lightOuterCones  ? lightOuterCones[i]  : 0.8192f;
        l->type      = lightTypes       ? lightTypes[i]        : LIGHT_TYPE_POINT;
        l->_pad[0]   = 0.0f;
        l->_pad[1]   = 0.0f;
        count++;
    }

    rendererUploadLights(renderer, lights, count);
}

static void renderGameScene()
{
    // Ensure framebuffer exists before rendering
    if (viewportFBs[activeFBO].fbo == 0 || viewportWidth == 0 || viewportHeight == 0)
    {
        // Framebuffer not initialized yet, skip rendering
        return;
    }

    f32 dt = (f32)(1.0 / (editor->fps > 0.0 ? editor->fps : 60.0));

    // Start a new gizmo draw batch for this frame
    gizmoBeginFrame();

    // ── Sync camera into the renderer's active camera slot ──
    // Edit mode: always sync the editor free-cam (sceneCam).
    // Play mode: the game DLL / ECS systems own the camera — do NOT
    //            overwrite it here.  The initial position is set from the
    //            scene camera entity in doBuildAndRun().
    if (!g_gameRunning && renderer && g_editorCamSlot != (u32)-1)
    {
        Camera *rCam = rendererGetCamera(renderer, g_editorCamSlot);
        if (rCam) *rCam = sceneCam;
    }

    // Push the renderer's active camera + timing into the core UBO
    // In play mode, the game plugin may have updated the camera pointer,
    // so call rendererBeginFrame AFTER game plugin gets a chance to move
    // the camera.
    if (renderer)
        rendererBeginFrame(renderer, dt);

    bindFramebuffer(&viewportFBs[activeFBO]);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.2f, 0.4f, 0.8f, 1.0f); // Blue clear color to test scene rendering
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ── Play mode: render scene, then hand off to game plugin + ECS ──
    if (g_gameRunning && g_gameDLL.loaded)
    {
        b8 deferred = renderer && renderer->useDeferredRendering
                      && renderer->activeGBuffer != (u32)-1;

        // If deferred, bind GBuffer for all geometry (scene + ECS)
        if (deferred)
            rendererBeginDeferredPass(renderer);

        // Draw skybox (always forward — doesn't write to GBuffer)
        if (!deferred)
        {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glUseProgram(skyboxShader);
            glBindVertexArray(skyboxMesh->vao);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }

        // Draw scene entities
        {
        PROFILE_SCOPE("Scene Render");
        Transform newTransform = {0};
        u32 lastBoundShader = 0;
        MaterialUniforms cachedUniforms = {0};
        for (u32 id = 0; id < entityCount; id++)
        {
            if (!isActive[id]) continue;

            // In play mode, entities assigned to ECS archetypes are rendered
            // in the ECS render pass below. Skip them here to prevent
            // double-rendering (ghost/duplicate models with mismatched materials).
            if (archetypeIDs)
            {
                u32 aid = archetypeIDs[id];
                if (aid != (u32)-1
                    && aid < MAX_ARCHETYPE_SYSTEMS
                    && g_ecsSystemLoaded[aid]
                    && g_ecsArchetypes[aid].arena)
                    continue;
            }

            newTransform = {positions[id], rotations[id], scales[id]};

            u32 modelID = modelIDs[id];
            if (modelID == (u32)-1 || modelID >= resources->modelUsed)
            {
                if (sceneCameraFlags && sceneCameraFlags[id])
                {
                    Transform marker = {positions[id], rotations[id], {0.2f, 0.2f, 0.35f}};
                    drawSceneCameraMarker(marker);
                }
                continue;
            }

            Model *model = &resources->modelBuffer[modelID];
            if (!model) continue;

            u32 entityShader = shaderHandles[id];
            u32 shaderToUse = (entityShader != 0) ? entityShader : shader;

            // Only re-bind shader + re-query uniform locations when shader changes
            if (shaderToUse != lastBoundShader)
            {
                glUseProgram(shaderToUse);
                cachedUniforms = getMaterialUniforms(shaderToUse);
                lastBoundShader = shaderToUse;
            }
            updateShaderModel(shaderToUse, newTransform);

            for (u32 i = 0; i < model->meshCount; i++)
            {
                u32 meshIndex = model->meshIndices[i];
                if (meshIndex >= resources->meshUsed) continue;

                Mesh *mesh = &resources->meshBuffer[meshIndex];
                if (!mesh || mesh->vao == 0) continue;

                u32 materialIndex = model->materialIndices[i];
                if (entityMaterialIDs && entityMaterialIDs[id] != (u32)-1)
                    materialIndex = entityMaterialIDs[id];
                if (materialIndex < resources->materialUsed)
                {
                    Material *material = &resources->materialBuffer[materialIndex];
                    updateMaterial(material, &cachedUniforms);
                }
                drawMesh(mesh);
            }
        }
        } // PROFILE_SCOPE("Scene Render")

        // Run ECS system render callbacks (custom rendering per archetype)
        // If the archetype has no custom render, fall back to the default forward pass.
        // All geometry goes to the same render target (GBuffer if deferred, viewport FBO if forward).
        {
        PROFILE_SCOPE("ECS Render");
        for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
        {
            if (g_ecsArchEntityCount[a] == 0 && !FLAG_CHECK(g_ecsArchetypes[a].flags, ARCH_BUFFERED)) continue;
            if (g_ecsSystemLoaded[a] && g_ecsSystems[a].render)
                g_ecsSystems[a].render(&g_ecsArchetypes[a], renderer);
            else
                rendererDefaultArchetypeRender(&g_ecsArchetypes[a], renderer);
        }
        // Game plugin render — called here so it writes into the correct FBO / GBuffer,
        // not before the FBO bind where its output would be cleared immediately.
        if (g_gameDLL.loaded)
            g_gameDLL.plugin.render(dt);
        } // PROFILE_SCOPE("ECS Render")

        // End deferred geometry pass, run lighting, then draw skybox on top
        if (deferred)
        {
            rendererEndDeferredPass(renderer);

            // Upload scene lights before lighting pass
            uploadSceneLights();

            // Lighting pass renders to the viewport FBO
            bindFramebuffer(&viewportFBs[activeFBO]);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            rendererLightingPass(renderer, deferredLightingShader);

            // Draw skybox after lighting (writes directly to viewport FBO)
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glUseProgram(skyboxShader);
            glBindVertexArray(skyboxMesh->vao);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }

        // Draw collider gizmos for all live physics archetypes (play mode, debug only)
        if (showColliders && !g_gameRunning)
        {
            for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
            {
                if (!FLAG_CHECK(g_archRegistry.entries[a].flags, ARCH_PHYSICS_BODY)) continue;

                Archetype *physArch = &g_ecsArchetypes[a];
                if (!physArch->layout) continue;

                ColliderFieldCache &fc = g_colliderCache[a];
                if (!fc.resolved)
                {
                    fc.posX = fc.posY = fc.posZ = -1;
                    fc.radius = fc.halfX = fc.halfY = fc.halfZ = -1;
                    StructLayout *layout = physArch->layout;
                    for (u32 f = 0; f < layout->count; f++)
                    {
                        const c8 *n = layout->fields[f].name;
                        if      (strcmp(n, "PositionX")     == 0) fc.posX   = (i32)f;
                        else if (strcmp(n, "PositionY")     == 0) fc.posY   = (i32)f;
                        else if (strcmp(n, "PositionZ")     == 0) fc.posZ   = (i32)f;
                        else if (strcmp(n, "SphereRadius")  == 0) fc.radius = (i32)f;
                        else if (strcmp(n, "ColliderHalfX") == 0) fc.halfX  = (i32)f;
                        else if (strcmp(n, "ColliderHalfY") == 0) fc.halfY  = (i32)f;
                        else if (strcmp(n, "ColliderHalfZ") == 0) fc.halfZ  = (i32)f;
                    }
                    fc.resolved = true;
                }
                if (fc.posX < 0 || fc.posY < 0 || fc.posZ < 0) continue;

                for (u32 c = 0; c < physArch->activeChunkCount; c++)
                {
                    void **fields = getArchetypeFields(physArch, c);
                    if (!fields) continue;
                    u32 count = physArch->arena[c].count;
                    f32 *posX  = (f32 *)fields[fc.posX];
                    f32 *posY  = (f32 *)fields[fc.posY];
                    f32 *posZ  = (f32 *)fields[fc.posZ];
                    f32 *radii = (fc.radius >= 0) ? (f32 *)fields[fc.radius] : NULL;
                    f32 *halfX = (fc.halfX  >= 0) ? (f32 *)fields[fc.halfX]  : NULL;
                    f32 *halfY = (fc.halfY  >= 0) ? (f32 *)fields[fc.halfY]  : NULL;
                    f32 *halfZ = (fc.halfZ  >= 0) ? (f32 *)fields[fc.halfZ]  : NULL;

                    for (u32 e = 0; e < count; e++)
                    {
                        Vec3 pos = {posX[e], posY[e], posZ[e]};
                        if (radii && radii[e] > 0.0f)
                            gizmoDrawSphere(pos, radii[e], makeGizmoColor(0.0f, 1.0f, 0.0f, 1.0f));
                        if (halfX && halfX[e] > 0.0f)
                        {
                            Vec3 half = {halfX[e],
                                         halfY ? halfY[e] : 0.5f,
                                         halfZ ? halfZ[e] : 0.5f};
                            gizmoDrawBox(pos, half, makeGizmoColor(0.0f, 1.0f, 0.0f, 1.0f));
                        }
                    }
                }
            }
        }

        // Flush gizmo overlay (draws on top with depth test disabled)
        {
            Camera *activeCam = renderer ? rendererGetCamera(renderer, renderer->activeCamera) : nullptr;
            Camera *cam = activeCam ? activeCam : &sceneCam;
            Mat4 view = getView(cam, false);
            Mat4 vp = mat4Mul(cam->projection, view);
            gizmoEndFrame(vp);
        }

        unbindFramebuffer();
        glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        return;
    }

    // ── Edit mode: render editor scene + gizmos (deferred pipeline) ──

    b8 editDeferred = renderer && renderer->useDeferredRendering
                      && renderer->activeGBuffer != (u32)-1
                      && deferredLightingShader != 0;

    // Begin deferred geometry pass — all scene geometry writes into the GBuffer
    if (editDeferred)
        rendererBeginDeferredPass(renderer);

    {
    Transform newTransform = {0};
    u32 lastBoundShader = 0;
    MaterialUniforms cachedUniforms = {0};
    for (u32 id = 0; id < entityCount; id++)
    {
        if (!isActive[id])
            continue;

        newTransform = {positions[id], rotations[id], scales[id]};

        u32 modelID = modelIDs[id];
        if (modelID == (u32)-1)
        {
            if (sceneCameraFlags && sceneCameraFlags[id])
            {
                Transform marker = {positions[id], rotations[id], {0.2f, 0.2f, 0.35f}};
                drawSceneCameraMarker(marker);
            }
            continue;
        }
        if (modelID < resources->modelUsed)
        {
            Model* model = &resources->modelBuffer[modelID];
            if (!model)
            {
                ERROR("Model pointer is NULL for entity %d, modelID %d", id, modelID);
                continue;
            }

            u32 entityShader = shaderHandles[id];
            u32 shaderToUse = (entityShader != 0) ? entityShader : shader;

            if (shaderToUse != lastBoundShader)
            {
                glUseProgram(shaderToUse);
                cachedUniforms = getMaterialUniforms(shaderToUse);
                lastBoundShader = shaderToUse;
            }
            updateShaderModel(shaderToUse, newTransform);

            for (u32 i = 0; i < model->meshCount; i++)
            {
                u32 meshIndex = model->meshIndices[i];
                if (meshIndex >= resources->meshUsed) continue;

                Mesh* mesh = &resources->meshBuffer[meshIndex];
                if (!mesh || mesh->vao == 0) continue;

                u32 materialIndex = model->materialIndices[i];
                if (entityMaterialIDs && entityMaterialIDs[id] != (u32)-1)
                    materialIndex = entityMaterialIDs[id];
                if (materialIndex < resources->materialUsed)
                {
                    Material* material = &resources->materialBuffer[materialIndex];
                    updateMaterial(material, &cachedUniforms);
                }
                drawMesh(mesh);
            }
        }
    }
    }

    if (editDeferred)
    {
        // End geometry pass, run lighting into the viewport FBO
        rendererEndDeferredPass(renderer);

        uploadSceneLights();

        bindFramebuffer(&viewportFBs[activeFBO]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        rendererLightingPass(renderer, deferredLightingShader);

        // Skybox after lighting — GBuffer depth was blitted by rendererLightingPass
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glUseProgram(skyboxShader);
        glBindVertexArray(skyboxMesh->vao);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }
    else
    {
        // Forward fallback: draw skybox first
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glUseProgram(skyboxShader);
        glBindVertexArray(skyboxMesh->vao);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    // Draw collider gizmos for all scene entities with a configured collider (edit mode)
    if (showColliders && colliderShapes)
    {
        for (u32 id = 0; id < entityCount; id++)
        {
            if (!isActive[id] || colliderShapes[id] == 0) continue;

            Vec3 pos = positions[id];
            GizmoColor col = (manipulateTransform && id == (u32)inspectorEntityID)
                             ? makeGizmoColor(1.0f, 1.0f, 0.0f, 1.0f)
                             : makeGizmoColor(0.0f, 1.0f, 0.0f, 1.0f);

            if (colliderShapes[id] == 1 && sphereRadii) // Sphere
            {
                f32 r = sphereRadii[id] > 0.0f ? sphereRadii[id] : 0.5f;
                gizmoDrawSphere(pos, r, col);
            }
            else if (colliderShapes[id] == 2 && colliderHalfXs) // Box
            {
                f32 hx = colliderHalfXs[id] > 0.0f ? colliderHalfXs[id] : 0.5f;
                f32 hy = colliderHalfYs[id] > 0.0f ? colliderHalfYs[id] : 0.5f;
                f32 hz = colliderHalfZs[id] > 0.0f ? colliderHalfZs[id] : 0.5f;
                Vec3 half = {hx, hy, hz};
                gizmoDrawBox(pos, half, col);
            }
        }
    }

    // Draw light gizmos for all scene entities marked as lights (edit mode)
    if (showLightGizmos && isLight)
    {
        for (u32 id = 0; id < entityCount; id++)
        {
            if (!isActive[id] || !isLight[id]) continue;

            Vec3 pos = positions[id];
            f32 cr = lightColorRs ? lightColorRs[id] : 1.0f;
            f32 cg = lightColorGs ? lightColorGs[id] : 1.0f;
            f32 cb = lightColorBs ? lightColorBs[id] : 1.0f;
            GizmoColor col = makeGizmoColor(cr, cg, cb, 1.0f);
            u32 lt = lightTypes ? lightTypes[id] : 0;

            if (lt == LIGHT_TYPE_POINT)
            {
                f32 r = lightRanges ? lightRanges[id] * 0.1f : 0.3f;
                if (r < 0.1f) r = 0.1f;
                if (r > 2.0f) r = 2.0f;
                gizmoDrawSphere(pos, r, col);
            }
            else if (lt == LIGHT_TYPE_DIRECTIONAL)
            {
                Vec3 dir = {
                    lightDirXs ? lightDirXs[id] : 0.0f,
                    lightDirYs ? lightDirYs[id] : -1.0f,
                    lightDirZs ? lightDirZs[id] : 0.0f
                };
                Vec3 end = v3Add(pos, v3Scale(dir, 2.0f));
                gizmoDrawArrow(pos, end, 0.15f, col);
            }
            else if (lt == LIGHT_TYPE_SPOT)
            {
                Vec3 dir = {
                    lightDirXs ? lightDirXs[id] : 0.0f,
                    lightDirYs ? lightDirYs[id] : -1.0f,
                    lightDirZs ? lightDirZs[id] : 0.0f
                };
                Vec3 end = v3Add(pos, v3Scale(dir, 2.0f));
                gizmoDrawArrow(pos, end, 0.15f, col);
                f32 outerCos = lightOuterCones ? lightOuterCones[id] : 0.8192f;
                f32 coneRadius = sqrtf(1.0f - outerCos * outerCos) / (outerCos > 0.01f ? outerCos : 0.01f);
                gizmoDrawCircle(end, dir, coneRadius, col);
            }
        }
    }

    // Gizmo handles (always forward, drawn on top)
    if (manipulateTransform)
    {
        Vec3 pos = positions[inspectorEntityID];

        f32 dist = v3Mag(v3Sub(pos, sceneCam.pos));
        if (dist < 1.0f) dist = 1.0f;
        f32 gizmoScale = dist * 0.12f;

        const f32 scaleSize   = 0.06f * gizmoScale;
        const f32 scaleLength = 1.0f  * gizmoScale;
        const f32 offset      = 0.5f  * gizmoScale;

        Vec3 offX = {offset, 0.0f,    0.0f};
        Vec3 offY = {0.0f,   offset,  0.0f};
        Vec3 offZ = {0.0f,   0.0f,   -offset};
        Transform X = {v3Add(pos, offX), quatIdentity(), {scaleLength, scaleSize, scaleSize}};
        Transform Y = {v3Add(pos, offY), quatIdentity(), {scaleSize, scaleLength, scaleSize}};
        Transform Z = {v3Add(pos, offZ), quatIdentity(), {scaleSize, scaleSize, scaleLength}};

        glDisable(GL_DEPTH_TEST);
        glUseProgram(arrowShader);

        updateShaderModel(arrowShader, X);
        glUniform3f(colourLocation, 1.0f, 0.2f, 0.2f);
        drawMesh(cubeMesh);

        updateShaderModel(arrowShader, Y);
        glUniform3f(colourLocation, 0.2f, 1.0f, 0.2f);
        drawMesh(cubeMesh);

        updateShaderModel(arrowShader, Z);
        glUniform3f(colourLocation, 0.2f, 0.4f, 1.0f);
        drawMesh(cubeMesh);

        glEnable(GL_DEPTH_TEST);
    }

    // Flush gizmo overlay (draws on top with depth test disabled)
    {
        Camera *gizmoCam = &sceneCam;
        if (g_gameRunning && renderer)
        {
            Camera *activeCam = rendererGetCamera(renderer, renderer->activeCamera);
            if (activeCam) gizmoCam = activeCam;
        }

        Mat4 view = getView(gizmoCam, false);
        Mat4 vp = mat4Mul(gizmoCam->projection, view);
        gizmoEndFrame(vp);
    }

    unbindFramebuffer();
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
}

ImVec2 g_viewportScreenPos = ImVec2(0, 0);
ImVec2 g_viewportSize = ImVec2(0, 0);

static void drawViewportWindow()
{
    ImGui::Begin("Viewport");

    ImVec2 viewportWindowPos =
        ImGui::GetWindowPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    canMoveViewPort =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    const f32 targetAspect = 16.0f / 9.0f;
    f32 targetW = avail.x;
    f32 targetH = avail.x / targetAspect;
    if (targetH > avail.y)
    {
        targetH = avail.y;
        targetW = targetH * targetAspect;
    }

    // Only resize FBOs when dimensions actually change
    static i32 s_lastFBOW = 0, s_lastFBOH = 0;
    if ((i32)targetW != s_lastFBOW || (i32)targetH != s_lastFBOH)
    {
        resizeViewportFramebuffers((i32)targetW, (i32)targetH);
        resizeIDFramebuffer((i32)targetW, (i32)targetH);
        s_lastFBOW = (i32)targetW;
        s_lastFBOH = (i32)targetH;
    }

    ImVec2 cursor = ImGui::GetCursorPos();
    ImVec2 imageOffset =
        ImVec2((avail.x - targetW) * 0.5f, (avail.y - targetH) * 0.5f);
    ImGui::SetCursorPos(
        ImVec2(cursor.x + imageOffset.x, cursor.y + imageOffset.y));

    // Save image position for mouse picking — must include the content region
    // offset (title bar + padding) so viewport-relative coords are correct.
    g_viewportScreenPos = ImGui::GetCursorScreenPos();
    g_viewportSize = ImVec2(targetW, targetH);

    // update camera projection — values driven by Camera Settings panel
    sceneCam.fovDeg   = g_editorFov;
    sceneCam.aspect   = targetAspect;
    sceneCam.nearClip = g_editorNear;
    sceneCam.farClip  = g_editorFar;
    sceneCam.projection =
        mat4Perspective(radians(g_editorFov), targetAspect, g_editorNear, g_editorFar);

    // Sync camera into renderer and push UBO BEFORE the ID pass so that
    // entity/gizmo picking uses the current frame's view/projection matrices.
    // (rendererBeginFrame inside renderGameScene would be too late.)
    if (renderer && !g_gameRunning && g_editorCamSlot != (u32)-1)
    {
        Camera *rCam = (Camera *)bufferGet(&renderer->cameras, g_editorCamSlot);
        if (rCam)
        {
            *rCam = sceneCam;
            Mat4 view = getView(rCam, false);
            updateCoreShaderUBO(renderer->time, &rCam->pos, &view, &rCam->projection);
        }
    }

    renderIDPass();
    renderGameScene();

    // Debug: Show what's happening with the conditions
        b8 shaderReady = (fboShader != 0);
        b8 finalFBOReady = (finalDisplayFB.fbo != 0);  
        b8 activeFBOReady = (viewportFBs[activeFBO].fbo != 0);    // Post-processing pipeline: render active FBO through shader to final display FBO
    if (shaderReady && finalFBOReady && activeFBOReady) {
        // Bind final display FBO as render target
        bindFramebuffer(&finalDisplayFB);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Render active FBO through post-processing shader
        renderFBOToScreen(activeFBO, fboShader);
        
        // Unbind FBO
        unbindFramebuffer();
        
        // Display the processed result
        ImGui::Image((void *)(intptr_t)finalDisplayFB.texture, ImVec2(targetW, targetH),
                     ImVec2(0, 1), ImVec2(1, 0));
    } else {
        // Fallback to raw texture if available
        if (activeFBOReady) {
            ImGui::Image((void *)(intptr_t)viewportFBs[activeFBO].texture, ImVec2(targetW, targetH),
                         ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::Text("FBOs not ready - Viewport: %dx%d", viewportWidth, viewportHeight);
        }
    }

    ImGui::End();

}

static void drawDebugWindow()
{
    const c8 *inspectorStateName = "UNKNOWN";
    switch (currentInspectorState)
    {
    case EMPTY_VIEW:  inspectorStateName = "EMPTY_VIEW";  break;
    case ENTITY_VIEW: inspectorStateName = "ENTITY_VIEW"; break;
    case SKYBOX_VIEW: inspectorStateName = "SKYBOX_VIEW"; break;
    default: break;
    }
    ImGui::Begin("Debug");

    // VSync toggle
    {
        static bool vsyncOn = false;
        if (ImGui::Checkbox("VSync", &vsyncOn))
            setVSync(vsyncOn ? 1 : 0);
    }
    ImGui::Separator();

    ImGui::Text("FPS %lf", editor->fps);
    ImGui::Text("Entity Count: %d", entityCount);
    ImGui::Text("Entity Size: %d", entitySize);
    ImGui::Text("Inspector Entity ID: %d", inspectorEntityID);
    ImGui::Text("Inspector State: %s", inspectorStateName);
    ImGui::Text("Viewport Size: %u x %u", viewportWidth, viewportHeight);
    
    // Multi-FBO System Info
    ImGui::Separator();
    ImGui::Text("Multi-FBO System");
    ImGui::Text("Active FBO: %d (ID: %d)", activeFBO, viewportFBs[activeFBO].fbo);
    ImGui::Text("Final Display FBO: ID %d", finalDisplayFB.fbo);
    ImGui::Text("FBO Shader: ID %d", fboShader);
    ImGui::Text("Screen Quad: %s", screenQuadMesh ? "Created" : "NULL");
    ImGui::Text("FBOs Created: %d/%d", 
               (viewportFBs[0].fbo != 0) + (viewportFBs[1].fbo != 0) + (viewportFBs[2].fbo != 0) + (viewportFBs[3].fbo != 0),
               MAX_FBOS);
    
    // Scene rendering debug info
    ImGui::Separator();
    ImGui::Text("Scene Content");
    ImGui::Text("Skybox Mesh: %s (VAO: %d)", skyboxMesh ? "Loaded" : "NULL", skyboxMesh ? skyboxMesh->vao : 0);
    ImGui::Text("Skybox Shader: %d", skyboxShader);
    ImGui::Text("Entities: %d/%d active", entityCount, entitySizeCache);
    if (entityCount > 0) {
        ImGui::Text("First entity model ID: %d", modelIDs[0]);
    }
    // input axis
    ImGui::Text("Input Axis: (%.2f, %.2f)", xInputAxis, yInputAxis);
    ImGui::Text("Mouse Position: (%.2f, %.2f)", ImGui::GetMousePos().x,
                ImGui::GetMousePos().y);

    if (resources)
    {
        // resource manager info
        ImGui::Text("Resources:");
        ImGui::Text("Meshes: %d : %d", resources->meshUsed,
                    resources->meshCount);
        ImGui::Text("Textures: %d : %d", resources->textureUsed,
                    resources->textureCount);
        ImGui::Text("Shaders: %d : %d", resources->shaderUsed,
                    resources->shaderCount);
        ImGui::Text("Materials: %d : %d", resources->materialUsed,
                    resources->materialCount);
        ImGui::Text("Models: %d : %d", resources->modelUsed,
                    resources->modelCount);
    }

    i32 dummy = -1; // No selection
    (void)dummy;
    // ImGui::ListBox("Console", &dummy, consoleLines, MAX_CONSOLE_LINES, 10);
    ImGui::End();
}

static void drawConsoleWindow()
{
    ImGui::Begin("Output");

    if (ImGui::Button("Clear"))
    {
        for (u32 i = 0; i < g_consoleCount; i++)
        {
            free((void *)consoleLines[i]);
            consoleLines[i] = NULL;
        }
        g_consoleCount = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy All"))
    {
        // Build combined string and copy to clipboard
        size_t totalLen = 0;
        for (u32 i = 0; i < g_consoleCount; i++)
            totalLen += strlen(consoleLines[i]) + 1; // +1 for newline
        if (totalLen > 0)
        {
            c8 *buf = (c8 *)malloc(totalLen + 1);
            if (buf)
            {
                size_t offset = 0;
                for (u32 i = 0; i < g_consoleCount; i++)
                {
                    size_t len = strlen(consoleLines[i]);
                    memcpy(buf + offset, consoleLines[i], len);
                    offset += len;
                    buf[offset++] = '\n';
                }
                buf[offset] = '\0';
                ImGui::SetClipboardText(buf);
                free(buf);
            }
        }
    }
    ImGui::SameLine();
    static b8 autoScroll = true;
    ImGui::Checkbox("Auto-scroll", (bool *)&autoScroll);
    ImGui::Separator();

    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    {
        ImGuiListClipper clipper;
        clipper.Begin((int)g_consoleCount);
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
                const c8 *line = consoleLines[i];
                if (!line) break;

                ImVec4 col = ImVec4(1, 1, 1, 1);
                if (strstr(line, "[ERROR]") || strstr(line, "[FATAL]"))
                    col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                else if (strstr(line, "[WARNING]"))
                    col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                else if (strstr(line, "[DEBUG]") || strstr(line, "[TRACE]"))
                    col = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();

                // Right-click any line to copy it
                ImGui::PushID(i);
                if (ImGui::BeginPopupContextItem("##linecopy", ImGuiPopupFlags_MouseButtonRight))
                {
                    if (ImGui::MenuItem("Copy Line"))
                        ImGui::SetClipboardText(line);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
    }
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Scan the project src/ directory for archetype system files and populate
// g_archRegistry so the archetype designer shows already-generated
// archetypes on project open.
//
// Detection heuristic: any .c file in src/ that contains the marker string
//   "FieldInfo <Name>_fields[]"
// is treated as an archetype system file. We also parse the preceding comment
// block for field indices, names, and C types so the registry entry holds a
// usable layout.
// ---------------------------------------------------------------------------
static b8 hasArchetypeMarkerLine(const c8 *text, const c8 *marker)
{
    if (!text || !marker) return false;

    const c8 *p = text;
    const u32 markerLen = (u32)strlen(marker);

    while (*p)
    {
        // Find start of current line
        const c8 *line = p;

        // Skip optional indentation
        while (*line == ' ' || *line == '\t') line++;

        // Expect exact marker line: // <marker>
        if (line[0] == '/' && line[1] == '/')
        {
            line += 2;
            while (*line == ' ' || *line == '\t') line++;

            if (strncmp(line, marker, markerLen) == 0)
            {
                const c8 tail = line[markerLen];
                if (tail == '\0' || tail == '\r' || tail == '\n')
                    return true;
            }
        }

        // Advance to next line
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    return false;
}

void scanProjectArchetypes(const c8 *projectDir)
{
    if (!projectDir || projectDir[0] == '\0') return;

    c8 srcDir[MAX_PATH_LENGTH];
    snprintf(srcDir, sizeof(srcDir), "%s/src", projectDir);

    u32 totalFiles = 0;
    c8 **files = listFilesInDirectory(srcDir, &totalFiles);
    if (!files || totalFiles == 0) return;

    for (u32 fi = 0; fi < totalFiles; fi++)
    {
        const c8 *path = files[fi];
        u32 plen = (u32)strlen(path);

        // look at .c and .cpp files
        b8 isCFile   = (plen >= 2 && strcmp(path + plen - 2, ".c")   == 0);
        b8 isCppFile = (plen >= 4 && strcmp(path + plen - 4, ".cpp") == 0);
        if (!isCFile && !isCppFile)
        {
            free(files[fi]);
            continue;
        }

        FILE *f = fopen(path, "r");
        if (!f) { free(files[fi]); continue; }

        // Read the file into memory (limit to 64 KB)
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 65536) { fclose(f); free(files[fi]); continue; }

        c8 *buf = (c8 *)malloc((u32)sz + 1);
        fread(buf, 1, (u32)sz, f);
        buf[sz] = '\0';
        fclose(f);

        // Detect archetype source files — two supported formats:
        //   Old: DSAPI FieldInfo Name_fields[] = { ... }
        //   New: DEFINE_ARCHETYPE(Name, NAME_FIELDS)
        c8 archName[MAX_SCENE_NAME] = {0};
        b8 isNewFormat = false;

        const c8 *marker = strstr(buf, "FieldInfo ");
        if (marker && strstr(marker, "_fields[]"))
        {
            // Old format — extract name between "FieldInfo " and "_fields[]"
            const c8 *nameStart = marker + strlen("FieldInfo ");
            const c8 *nameEnd   = strstr(nameStart, "_fields[]");
            if (!nameEnd || nameEnd <= nameStart)
            {
                free(buf);
                free(files[fi]);
                continue;
            }
            u32 nameLen = (u32)(nameEnd - nameStart);
            if (nameLen >= MAX_SCENE_NAME) nameLen = MAX_SCENE_NAME - 1;
            memcpy(archName, nameStart, nameLen);
            archName[nameLen] = '\0';
        }
        else
        {
            // New format — look for DEFINE_ARCHETYPE(Name, ...)
            const c8 *da = strstr(buf, "DEFINE_ARCHETYPE(");
            if (!da)
            {
                free(buf);
                free(files[fi]);
                continue;
            }
            da += strlen("DEFINE_ARCHETYPE(");
            // skip whitespace
            while (*da == ' ' || *da == '\t') da++;
            const c8 *daEnd = da;
            while (*daEnd && *daEnd != ',' && *daEnd != ')' && *daEnd != ' ' && *daEnd != '\t') daEnd++;
            u32 nameLen = (u32)(daEnd - da);
            if (nameLen == 0 || nameLen >= MAX_SCENE_NAME)
            {
                free(buf);
                free(files[fi]);
                continue;
            }
            memcpy(archName, da, nameLen);
            archName[nameLen] = '\0';
            isNewFormat = true;
        }

        // Skip "game" or "game.c" - that's the main game plugin, not an archetype
        if (strcmp(archName, "game") == 0)
        {
            free(buf);
            free(files[fi]);
            continue;
        }

        // Check if already registered
        b8 alreadyRegistered = false;
        for (u32 r = 0; r < g_archRegistry.count; r++)
        {
            if (strcmp(g_archRegistry.entries[r].name, archName) == 0)
            { alreadyRegistered = true; break; }
        }
        if (alreadyRegistered || g_archRegistry.count >= MAX_ARCHETYPE_SYSTEMS)
        {
            free(buf);
            free(files[fi]);
            continue;
        }

        // Parse field entries from { "name", sizeof(type) } patterns
        // and populate the registry layout with proper FieldInfo data.
        ArchetypeFileEntry *entry = &g_archRegistry.entries[g_archRegistry.count];
        memset(entry, 0, sizeof(ArchetypeFileEntry));
        strncpy(entry->name, archName, MAX_SCENE_NAME - 1);
        snprintf(entry->headerPath, MAX_PATH_LENGTH, "src/%s.h", archName);
        snprintf(entry->sourcePath, MAX_PATH_LENGTH, "src/%s.c", archName);
        entry->layout.name = entry->name;

        u32 regIdx = g_archRegistry.count;

        // Parse field entries and store field info.
        // Old format: { "name", sizeof(type) } entries in the .c file.
        // New format: FIELD(ENUM, "name", type, HOT|COLD) entries in the .h file's FIELDS macro.
        u32 fieldCount = 0;

        if (!isNewFormat)
        {
            // --- Old format: parse from the already-loaded .c buffer ---
            const c8 *scan = strstr(buf, "_fields[] = {");
            if (scan)
            {
                scan = strchr(scan, '{');
                if (scan) scan++; // skip the opening {

                while (scan && fieldCount < 32)
                {
                    // find next field entry — accept both { "name" and {"name"
                    const c8 *e1 = strstr(scan, "{ \"");
                    const c8 *e2 = strstr(scan, "{\"");
                    const c8 *entryStart = NULL;
                    u32       nameOff    = 0;
                    if (e1 && e2) { entryStart = (e1 < e2) ? e1 : e2; nameOff = (entryStart == e1) ? 3 : 2; }
                    else if (e1)  { entryStart = e1; nameOff = 3; }
                    else if (e2)  { entryStart = e2; nameOff = 2; }
                    if (!entryStart) break;
                    const c8 *closeBrace = strstr(scan, "};");
                    if (closeBrace && entryStart > closeBrace) break;

                    const c8 *ns = entryStart + nameOff;
                    const c8 *ne = strchr(ns, '"');
                    if (ne && (u32)(ne - ns) < 128)
                    {
                        memset(g_scannedFieldNames[regIdx][fieldCount], 0, 128);
                        memcpy(g_scannedFieldNames[regIdx][fieldCount], ns, (u32)(ne - ns));
                    }

                    u32 parsedSize = 0;
                    const c8 *szOf = strstr(entryStart, "sizeof(");
                    if (szOf)
                    {
                        szOf += 7;
                        const c8 *szEnd = strchr(szOf, ')');
                        if (szEnd)
                        {
                            c8 typeBuf[64] = {0};
                            u32 tlen = (u32)(szEnd - szOf);
                            if (tlen < 64) memcpy(typeBuf, szOf, tlen);
                            parsedSize = knownTypeSize(typeBuf);
                        }
                    }

                    g_scannedFields[regIdx][fieldCount].name = g_scannedFieldNames[regIdx][fieldCount];
                    g_scannedFields[regIdx][fieldCount].size = parsedSize;

                    scan = entryStart + 3;
                    fieldCount++;

                    const c8 *entryEnd = strchr(scan, '}');
                    if (entryEnd) scan = entryEnd + 1;
                    else break;
                }
            }
        }
        else
        {
            // --- New format: parse FIELD(ENUM, "name", type, HOT|COLD) from the .h file ---
            // Build path to the .h file: same directory as the .c file, same base name.
            c8 hPath[MAX_PATH_LENGTH] = {0};
            strncpy(hPath, path, MAX_PATH_LENGTH - 1);
            u32 hLen = (u32)strlen(hPath);
            // Replace trailing ".c" or ".cpp" with ".h"
            if (hLen >= 4 && strcmp(hPath + hLen - 4, ".cpp") == 0)
                strcpy(hPath + hLen - 4, ".h");
            else if (hLen >= 2 && strcmp(hPath + hLen - 2, ".c") == 0)
                strcpy(hPath + hLen - 2, ".h");

            FILE *hf = fopen(hPath, "r");
            if (hf)
            {
                fseek(hf, 0, SEEK_END);
                long hsz = ftell(hf);
                fseek(hf, 0, SEEK_SET);
                if (hsz > 0 && hsz <= 65536)
                {
                    c8 *hbuf = (c8 *)malloc((u32)hsz + 1);
                    fread(hbuf, 1, (u32)hsz, hf);
                    hbuf[hsz] = '\0';
                    fclose(hf);

                    // Find the #define ARCHNAME_FIELDS(FIELD) macro
                    // Build the search token: "ARCHNAME_FIELDS("
                    c8 upperName[MAX_SCENE_NAME * 2] = {0};
                    for (u32 ci = 0; archName[ci]; ci++)
                        upperName[ci] = (archName[ci] >= 'a' && archName[ci] <= 'z')
                                        ? (c8)(archName[ci] - 32) : archName[ci];
                    c8 fieldsMacroToken[MAX_SCENE_NAME * 2 + 16] = {0};
                    snprintf(fieldsMacroToken, sizeof(fieldsMacroToken), "%s_FIELDS(", upperName);

                    const c8 *hScan = strstr(hbuf, fieldsMacroToken);
                    if (hScan)
                    {
                        // Walk through FIELD( entries until end of macro (no trailing backslash)
                        while (hScan && fieldCount < 32)
                        {
                            const c8 *fe = strstr(hScan, "FIELD(");
                            if (!fe) break;

                            // Skip past "FIELD("
                            fe += 6;
                            // Skip enum name argument (up to first comma)
                            const c8 *comma1 = strchr(fe, ',');
                            if (!comma1) break;
                            comma1++;
                            // Skip whitespace before the quoted name
                            while (*comma1 == ' ' || *comma1 == '\t') comma1++;
                            if (*comma1 != '"') { hScan = comma1; continue; }
                            const c8 *ns = comma1 + 1;
                            const c8 *ne = strchr(ns, '"');
                            if (!ne || (u32)(ne - ns) >= 128) { hScan = comma1 + 1; continue; }

                            memset(g_scannedFieldNames[regIdx][fieldCount], 0, 128);
                            memcpy(g_scannedFieldNames[regIdx][fieldCount], ns, (u32)(ne - ns));

                            // Skip past closing quote and comma to get the type argument
                            const c8 *comma2 = strchr(ne + 1, ',');
                            u32 parsedSize = 0;
                            if (comma2)
                            {
                                comma2++;
                                while (*comma2 == ' ' || *comma2 == '\t') comma2++;
                                const c8 *typeEnd = comma2;
                                while (*typeEnd && *typeEnd != ',' && *typeEnd != ')' &&
                                       *typeEnd != ' ' && *typeEnd != '\t' &&
                                       *typeEnd != '\r' && *typeEnd != '\n' && *typeEnd != '\\')
                                    typeEnd++;
                                c8 typeBuf[64] = {0};
                                u32 tlen = (u32)(typeEnd - comma2);
                                if (tlen > 0 && tlen < 64)
                                {
                                    memcpy(typeBuf, comma2, tlen);
                                    parsedSize = knownTypeSize(typeBuf);
                                }
                            }

                            g_scannedFields[regIdx][fieldCount].name = g_scannedFieldNames[regIdx][fieldCount];
                            g_scannedFields[regIdx][fieldCount].size = parsedSize;
                            fieldCount++;
                            hScan = ne + 1;
                        }
                    }

                    free(hbuf);
                }
                else
                {
                    fclose(hf);
                }
            }
        }

        entry->layout.count  = fieldCount;
        entry->layout.fields = g_scannedFields[regIdx];

        // Prefer canonical flags marker emitted by generateArchetypeFiles:
        //   // DRUID_FLAGS 0xNN
        b8 hasCanonicalFlags = false;
        const c8 *flagsMarker = strstr(buf, "// DRUID_FLAGS ");
        if (flagsMarker)
        {
            unsigned int parsedFlags = 0;
            if (sscanf(flagsMarker, "// DRUID_FLAGS %x", &parsedFlags) == 1)
            {
                entry->flags = (u8)(parsedFlags & 0xFFu);
                hasCanonicalFlags = true;
            }
        }

        // Legacy fallback for older generated files without DRUID_FLAGS marker.
        if (!hasCanonicalFlags)
        {
            if (hasArchetypeMarkerLine(buf, "isSingle"))
                FLAG_SET(entry->flags, ARCH_SINGLE);

            if (hasArchetypeMarkerLine(buf, "isBuffered"))
                FLAG_SET(entry->flags, ARCH_BUFFERED);

            if (hasArchetypeMarkerLine(buf, "isPhysicsBody"))
                FLAG_SET(entry->flags, ARCH_PHYSICS_BODY);
        }

        // ARCH_SINGLE + ARCH_PHYSICS_BODY is valid (e.g. Player archetype).
        // Previously this code would clear ARCH_SINGLE for physics bodies,
        // but the two flags are independent and can be combined.

        // Scan for POOL_CAPACITY define
        {
            const char *pcDef = strstr(buf, "#define POOL_CAPACITY ");
            if (pcDef) entry->poolCapacity = (u32)atoi(pcDef + 22);
        }

        // Detect uniform scale: field named "Scale" with sizeof(f32)
        if (fieldCount >= 3
            && strcmp(g_scannedFieldNames[regIdx][2], "Scale") == 0
            && g_scannedFields[regIdx][2].size == sizeof(f32))
            FLAG_SET(entry->flags, ARCH_FILE_UNIFORM_SCALE);

        g_archRegistry.count++;
        INFO("Scanned archetype: %s (%u fields) from %s", archName, fieldCount, path);

        free(buf);
        free(files[fi]);
    }

    free(files);

    // Auto-register built-in StaticObject archetype if not found in project files
    {
        b8 hasStaticObject = false;
        for (u32 a = 0; a < g_archRegistry.count; a++)
            if (strcmp(g_archRegistry.entries[a].name, "StaticObject") == 0)
            { hasStaticObject = true; break; }

        if (!hasStaticObject && g_archRegistry.count < MAX_ARCHETYPE_SYSTEMS)
        {
            u32 idx = g_archRegistry.count;
            ArchetypeFileEntry *entry = &g_archRegistry.entries[idx];
            strncpy(entry->name, "StaticObject", MAX_SCENE_NAME - 1);
            entry->headerPath[0] = '\0';
            entry->sourcePath[0] = '\0';
            entry->flags = 0;
            FLAG_SET(entry->flags, ARCH_PHYSICS_BODY);
            entry->poolCapacity = 0;

            static const c8 *soFieldNames[] = {
                "PositionX", "PositionY", "PositionZ", "Rotation", "Scale", "ModelID"
            };
            static const u32 soFieldSizes[] = {
                sizeof(f32), sizeof(f32), sizeof(f32), sizeof(Vec4), sizeof(Vec3), sizeof(u32)
            };
            u32 soCount = 6;

            for (u32 f = 0; f < soCount; f++)
            {
                strncpy(g_scannedFieldNames[idx][f], soFieldNames[f], 127);
                g_scannedFields[idx][f].name = g_scannedFieldNames[idx][f];
                g_scannedFields[idx][f].size = soFieldSizes[f];
                g_scannedFields[idx][f].temperature = (f < 5) ? FIELD_TEMP_HOT : FIELD_TEMP_COLD;
            }
            entry->layout.name   = entry->name;
            entry->layout.fields = g_scannedFields[idx];
            entry->layout.count  = soCount;
            g_archRegistry.count++;

            INFO("Auto-registered built-in archetype: StaticObject");
        }
    }
}

static void drawPrefabsWindow()
{
    // ---- archetype designer state (persists across frames) ----
    static c8  archName[128]           = "";
    static bool archIsSingle           = false;
    static bool archIsPersistent       = false;
    static bool archIsBuffered         = false;
    static bool archIsPhysicsBody      = false;
    static i32 archBufferSize          = 100;  // pool size for buffered archetypes

    // field editing table
    #define ARCH_MAX_FIELDS 32
    static c8  fieldNames[ARCH_MAX_FIELDS][128];
    static i32 fieldTypeIndex[ARCH_MAX_FIELDS]; // combo selection
    static u32 fieldCount                       = 0;
    static bool firstInit                       = true;

    if (firstInit)
    {
        memset(fieldNames, 0, sizeof(fieldNames));
        memset(fieldTypeIndex, 0, sizeof(fieldTypeIndex));
        firstInit = false;
    }

    // built-in type list — maps to sizeof used by generateArchetypeFiles
    static const c8 *typeNames[]  = { "f32", "i32", "u32", "b8", "Vec2", "Vec3", "Vec4", "Mat4", "u64", "c8[256]" };
    static const u32 typeSizes[]  = { 4,      4,     4,     1,    8,      12,     16,     64,     8,     256      };
    static const u32 typeCount    = sizeof(typeSizes) / sizeof(typeSizes[0]);

    // result message
    static c8  resultMsg[256] = "";

    ImGui::Begin("Archetypes");

    ImGui::Text("Archetype Designer");
    ImGui::Separator();

    ImGui::InputText("Name", archName, sizeof(archName));

    // isBuffered locks out isSingle (buffered pools are always multi-entity)
    const bool singleLockedByBuffered = archIsBuffered;
    if (singleLockedByBuffered && archIsSingle)
        archIsSingle = false;

    if (singleLockedByBuffered)
        ImGui::BeginDisabled();
    ImGui::Checkbox("isSingle", &archIsSingle);
    if (singleLockedByBuffered)
    {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Buffered archetypes cannot be single-instance.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("isPhysicsBody", &archIsPhysicsBody);
    ImGui::SameLine();
    ImGui::Checkbox("isPersistent", &archIsPersistent);
    ImGui::SameLine();
    if (ImGui::Checkbox("isBuffered", &archIsBuffered) && archIsBuffered)
        archIsSingle = false;

    if (archIsBuffered)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("Pool Size", &archBufferSize, 10, 1000);
        ImGui::TextDisabled("Buffered archetypes create a pool with hidden instances (Alive flag)");
    }

    // Transform is always included; offer uniform scale option
    static bool archUniformScale = false;
    static bool archUseCpp = false;
    ImGui::Checkbox("Uniform Scale (f32)", &archUniformScale);
    ImGui::Checkbox("Generate C++ (.cpp)", &archUseCpp);
    ImGui::TextDisabled("Auto-included: PositionX/Y/Z(f32), Rotation(Vec4), Scale(%s)",
                        archUniformScale ? "f32" : "Vec3");

    ImGui::Separator();
    ImGui::Text("Extra Fields (%u / %u)", fieldCount, (u32)ARCH_MAX_FIELDS);

    // field table
    if (ImGui::BeginTable("##fields", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableHeadersRow();

        u32 removeIdx = (u32)-1;
        for (u32 i = 0; i < fieldCount; i++)
        {
            ImGui::TableNextRow();
            ImGui::PushID((int)i);

            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##fn", fieldNames[i], sizeof(fieldNames[i]));

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##ft", &fieldTypeIndex[i], typeNames, (int)typeCount);

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("X"))
                removeIdx = i;

            ImGui::PopID();
        }
        ImGui::EndTable();

        // deferred remove to avoid iterator invalidation
        if (removeIdx != (u32)-1 && removeIdx < fieldCount)
        {
            for (u32 j = removeIdx; j < fieldCount - 1; j++)
            {
                memcpy(fieldNames[j], fieldNames[j + 1], sizeof(fieldNames[j]));
                fieldTypeIndex[j] = fieldTypeIndex[j + 1];
            }
            fieldCount--;
        }
    }

    if (fieldCount < ARCH_MAX_FIELDS)
    {
        if (ImGui::Button("+ Add Field"))
        {
            memset(fieldNames[fieldCount], 0, sizeof(fieldNames[fieldCount]));
            fieldTypeIndex[fieldCount] = 0;
            fieldCount++;
        }
    }

    ImGui::Separator();

    // Generate button — transform-only archetypes are valid (fieldCount may be 0)
    bool canGenerate = (archName[0] != '\0' && hubProjectDir[0] != '\0');
    if (!canGenerate) ImGui::BeginDisabled();

    if (ImGui::Button("Generate Archetype Files"))
    {
        // Prepend transform fields: PositionX/Y/Z(f32), Rotation(Vec4), Scale(Vec3|f32)
        static c8 tfNames[5][128] = {"PositionX", "PositionY", "PositionZ", "Rotation", "Scale"};
        const u32 tfCount = 5;
        const u32 totalFields = tfCount + fieldCount;

        FieldInfo allFields[ARCH_MAX_FIELDS + 5];
        const c8 *allTypes[ARCH_MAX_FIELDS + 5];

        allFields[0] = { tfNames[0], sizeof(f32), FIELD_TEMP_HOT }; allTypes[0] = "f32";
        allFields[1] = { tfNames[1], sizeof(f32), FIELD_TEMP_HOT }; allTypes[1] = "f32";
        allFields[2] = { tfNames[2], sizeof(f32), FIELD_TEMP_HOT }; allTypes[2] = "f32";
        allFields[3] = { tfNames[3], sizeof(Vec4), FIELD_TEMP_HOT }; allTypes[3] = "Vec4";
        if (archUniformScale) {
            allFields[4] = { tfNames[4], sizeof(f32), FIELD_TEMP_HOT }; allTypes[4] = "f32";
        } else {
            allFields[4] = { tfNames[4], sizeof(Vec3), FIELD_TEMP_HOT }; allTypes[4] = "Vec3";
        }

        for (u32 i = 0; i < fieldCount; i++)
        {
            allFields[tfCount + i].name = fieldNames[i];
            allFields[tfCount + i].size = typeSizes[fieldTypeIndex[i]];
            allFields[tfCount + i].temperature = FIELD_TEMP_COLD;
            allTypes[tfCount + i]       = typeNames[fieldTypeIndex[i]];
        }

        if (generateArchetypeFiles(hubProjectDir, archName, allFields, allTypes, totalFields, archIsSingle, archIsBuffered, (u32)archBufferSize, archIsPhysicsBody, archUseCpp))
        {
            snprintf(resultMsg, sizeof(resultMsg), "Generated %s archetype files.", archName);

            // Register in the editor-side archetype registry
            if (g_archRegistry.count < MAX_ARCHETYPE_SYSTEMS)
            {
                u32 regIdx = g_archRegistry.count;
                for (u32 r = 0; r < g_archRegistry.count; r++)
                {
                    if (strcmp(g_archRegistry.entries[r].name, archName) == 0)
                    { regIdx = r; break; }
                }
                ArchetypeFileEntry *entry = &g_archRegistry.entries[regIdx];
                strncpy(entry->name, archName, MAX_SCENE_NAME - 1);
                snprintf(entry->headerPath, MAX_PATH_LENGTH, "src/%s.h", archName);
                snprintf(entry->sourcePath, MAX_PATH_LENGTH, "src/%s.%s", archName, archUseCpp ? "cpp" : "c");
                entry->flags = 0;
                if (archIsSingle && !archIsBuffered) FLAG_SET(entry->flags, ARCH_SINGLE);
                if (archIsPersistent)  FLAG_SET(entry->flags, ARCH_PERSISTENT);
                if (archUniformScale)  FLAG_SET(entry->flags, ARCH_FILE_UNIFORM_SCALE);
                if (archIsBuffered)    FLAG_SET(entry->flags, ARCH_BUFFERED);
                if (archIsPhysicsBody) FLAG_SET(entry->flags, ARCH_PHYSICS_BODY);
                entry->poolCapacity  = (u32)archBufferSize;
                entry->layout.name  = entry->name;
                entry->layout.count = totalFields;
                // Copy scanned fields for the registry
                for (u32 i = 0; i < totalFields && i < 32; i++)
                {
                    strncpy(g_scannedFieldNames[regIdx][i], allFields[i].name, 127);
                    g_scannedFields[regIdx][i].name = g_scannedFieldNames[regIdx][i];
                    g_scannedFields[regIdx][i].size = allFields[i].size;
                }
                entry->layout.fields = g_scannedFields[regIdx];
                if (regIdx == g_archRegistry.count)
                    g_archRegistry.count++;
            }
        }
        else
            snprintf(resultMsg, sizeof(resultMsg), "Failed to generate archetype files!");
    }

    if (!canGenerate) ImGui::EndDisabled();

    if (resultMsg[0] != '\0')
    {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", resultMsg);
    }

    // ---- Quick-create templates ----
    ImGui::Separator();
    ImGui::Text("Templates");
    if (ImGui::Button("Create StaticObject") && hubProjectDir[0] != '\0')
    {
        // Check if StaticObject already exists
        b8 exists = false;
        for (u32 a = 0; a < g_archRegistry.count; a++)
            if (strcmp(g_archRegistry.entries[a].name, "StaticObject") == 0)
            { exists = true; break; }

        if (exists)
        {
            snprintf(resultMsg, sizeof(resultMsg), "StaticObject archetype already exists.");
        }
        else
        {
            FieldInfo soFields[] = {
                { "PositionX",      sizeof(f32),  FIELD_TEMP_HOT },
                { "PositionY",      sizeof(f32),  FIELD_TEMP_HOT },
                { "PositionZ",      sizeof(f32),  FIELD_TEMP_HOT },
                { "Rotation",       sizeof(Vec4), FIELD_TEMP_HOT },
                { "Scale",          sizeof(Vec3), FIELD_TEMP_HOT },
                { "ModelID",        sizeof(u32),  FIELD_TEMP_HOT },
            };
            const c8 *soTypes[] = { "f32", "f32", "f32", "Vec4", "Vec3", "u32" };
            u32 soCount = sizeof(soFields) / sizeof(soFields[0]);

            if (generateArchetypeFiles(hubProjectDir, "StaticObject",
                    soFields, soTypes, soCount,
                    false, false, 0, true, false))
            {
                snprintf(resultMsg, sizeof(resultMsg), "Generated StaticObject archetype.");
                scanProjectArchetypes(hubProjectDir);
            }
            else
                snprintf(resultMsg, sizeof(resultMsg), "Failed to generate StaticObject archetype.");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(isPhysicsBody, static collider, no code needed)");

    // ---- Registered Archetypes list ----
    ImGui::Separator();
    ImGui::Text("Registered Archetypes (%u)", g_archRegistry.count);
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan") && hubProjectDir[0] != '\0')
        scanProjectArchetypes(hubProjectDir);

    if (g_archRegistry.count > 0)
    {
        for (u32 a = 0; a < g_archRegistry.count; a++)
        {
            ArchetypeFileEntry *entry = &g_archRegistry.entries[a];
            ImGui::PushID((int)a + 1000);

            ImGui::BulletText("%s  (%u fields%s%s%s%s)",
                entry->name,
                entry->layout.count,
                FLAG_CHECK(entry->flags, ARCH_SINGLE) ? ", single" : "",
                FLAG_CHECK(entry->flags, ARCH_PERSISTENT) ? ", persistent" : "",
                FLAG_CHECK(entry->flags, ARCH_BUFFERED) ? ", buffered" : "",
                FLAG_CHECK(entry->flags, ARCH_PHYSICS_BODY) ? ", physics" : "");

            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                // Unassign this archetype from all scene entities that reference it.
                // Entities pointing at higher indices get their ID decremented to
                // keep the registry compact.
                if (archetypeIDs)
                {
                    for (u32 e = 0; e < entityCount; e++)
                    {
                        if (archetypeIDs[e] == a)
                            archetypeIDs[e] = (u32)-1;
                        else if (archetypeIDs[e] > a && archetypeIDs[e] != (u32)-1)
                            archetypeIDs[e]--;
                    }
                }

                // layout.fields points into g_scannedFields (static) — do not free()
                // Shift registry entries AND the parallel scanned-field arrays together
                // so layout.fields pointers remain valid after the shift.
                for (u32 r = a; r + 1 < g_archRegistry.count; r++)
                {
                    g_archRegistry.entries[r] = g_archRegistry.entries[r + 1];
                    memcpy(g_scannedFields[r],     g_scannedFields[r + 1],     sizeof(g_scannedFields[0]));
                    memcpy(g_scannedFieldNames[r], g_scannedFieldNames[r + 1], sizeof(g_scannedFieldNames[0]));
                    // Fix layout.name (points into entry->name) and
                    // layout.fields (points into the static row) after the copy.
                    g_archRegistry.entries[r].layout.name   = g_archRegistry.entries[r].name;
                    g_archRegistry.entries[r].layout.fields = g_scannedFields[r];
                    for (u32 f = 0; f < g_archRegistry.entries[r].layout.count; f++)
                        g_scannedFields[r][f].name = g_scannedFieldNames[r][f];
                }
                u32 last = g_archRegistry.count - 1;
                memset(&g_archRegistry.entries[last], 0, sizeof(ArchetypeFileEntry));
                memset(g_scannedFields[last],     0, sizeof(g_scannedFields[0]));
                memset(g_scannedFieldNames[last], 0, sizeof(g_scannedFieldNames[0]));
                g_archRegistry.count--;

                ImGui::PopID();
                break;  // iterator invalidated — exit the loop
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Edit"))
            {
                // Load this archetype into the designer fields
                strncpy(archName, entry->name, sizeof(archName) - 1);
                archIsSingle      = FLAG_CHECK(entry->flags, ARCH_SINGLE);
                archIsPersistent  = FLAG_CHECK(entry->flags, ARCH_PERSISTENT);
                archIsBuffered    = FLAG_CHECK(entry->flags, ARCH_BUFFERED);
                archIsPhysicsBody = FLAG_CHECK(entry->flags, ARCH_PHYSICS_BODY);
                archBufferSize    = entry->poolCapacity > 0 ? (i32)entry->poolCapacity : 100;

                // We need to re-parse the actual field data from the .c file
                // to populate fieldNames and fieldTypeIndex
                c8 srcPath[MAX_PATH_LENGTH];
                snprintf(srcPath, sizeof(srcPath), "%s/%s", hubProjectDir, entry->sourcePath);
                FILE *ef = fopen(srcPath, "r");
                if (ef)
                {
                    fseek(ef, 0, SEEK_END);
                    long esz = ftell(ef);
                    fseek(ef, 0, SEEK_SET);
                    if (esz > 0 && esz < 65536)
                    {
                        c8 *ebuf = (c8 *)malloc((u32)esz + 1);
                        fread(ebuf, 1, (u32)esz, ef);
                        ebuf[esz] = '\0';

                        // Parse { "name", sizeof(Type) } entries
                        u32 allFieldCount = 0;
                        c8  allFieldNames[ARCH_MAX_FIELDS + 3][128];
                        i32 allFieldTypeIdx[ARCH_MAX_FIELDS + 3];
                        memset(allFieldNames, 0, sizeof(allFieldNames));
                        memset(allFieldTypeIdx, 0, sizeof(allFieldTypeIdx));

                        const c8 *scan = strstr(ebuf, "_fields[] = {");
                        if (scan)
                        {
                            scan = strchr(scan, '{');
                            if (scan) scan++;

                            while (scan && allFieldCount < ARCH_MAX_FIELDS + 3)
                            {
                                const c8 *e1b = strstr(scan, "{ \"");
                                const c8 *e2b = strstr(scan, "{\"");
                                const c8 *es  = NULL; u32 nsOff = 0;
                                if (e1b && e2b) { es = (e1b < e2b) ? e1b : e2b; nsOff = (es == e1b) ? 3 : 2; }
                                else if (e1b)   { es = e1b; nsOff = 3; }
                                else if (e2b)   { es = e2b; nsOff = 2; }
                                if (!es) break;
                                const c8 *closeBr = strstr(scan, "};");
                                if (closeBr && es > closeBr) break;

                                const c8 *ns = es + nsOff;
                                const c8 *ne = strchr(ns, '"');
                                if (ne && (ne - ns) < 128)
                                    memcpy(allFieldNames[allFieldCount], ns, (u32)(ne - ns));

                                const c8 *szOf = strstr(es, "sizeof(");
                                if (szOf)
                                {
                                    szOf += 7;
                                    const c8 *szEnd = strchr(szOf, ')');
                                    if (szEnd)
                                    {
                                        c8 typeBuf[64] = {0};
                                        u32 tlen = (u32)(szEnd - szOf);
                                        if (tlen < 64) memcpy(typeBuf, szOf, tlen);
                                        allFieldTypeIdx[allFieldCount] = 0;
                                        for (u32 t = 0; t < typeCount; t++)
                                        {
                                            if (strcmp(typeBuf, typeNames[t]) == 0)
                                            { allFieldTypeIdx[allFieldCount] = (i32)t; break; }
                                        }
                                    }
                                }

                                allFieldCount++;
                                const c8 *entryEnd = strchr(es + 1, '}');
                                scan = entryEnd ? entryEnd + 1 : nullptr;
                            }
                        }

                        // Strip the auto-prepended transform fields (and the
                        // leading Alive field for buffered archetypes) so the
                        // designer only shows the user-defined extra fields.
                        archUniformScale = false;
                        u32 skipCount = 0;
                        u32 tfStart   = 0;

                        // Buffered archetypes have Alive as field[0]
                        if (allFieldCount >= 1 && strcmp(allFieldNames[0], "Alive") == 0)
                            tfStart = 1;

                        if (allFieldCount >= tfStart + 5
                            && strcmp(allFieldNames[tfStart + 0], "PositionX") == 0
                            && strcmp(allFieldNames[tfStart + 1], "PositionY") == 0
                            && strcmp(allFieldNames[tfStart + 2], "PositionZ") == 0
                            && strcmp(allFieldNames[tfStart + 3], "Rotation")  == 0
                            && strcmp(allFieldNames[tfStart + 4], "Scale")     == 0)
                        {
                            skipCount    = tfStart + 5;
                            // f32 is index 0 in typeNames[]
                            archUniformScale = (allFieldTypeIdx[tfStart + 4] == 0);
                        }
                        else if (tfStart > 0)
                        {
                            // Buffered but transform block not found — skip Alive only
                            skipCount = tfStart;
                        }

                        fieldCount = 0;
                        for (u32 j = skipCount; j < allFieldCount && fieldCount < ARCH_MAX_FIELDS; j++)
                        {
                            memcpy(fieldNames[fieldCount], allFieldNames[j], 128);
                            fieldTypeIndex[fieldCount] = allFieldTypeIdx[j];
                            fieldCount++;
                        }

                        free(ebuf);
                    }
                    fclose(ef);
                    snprintf(resultMsg, sizeof(resultMsg), "Loaded %s into designer.", entry->name);
                }
                else
                {
                    snprintf(resultMsg, sizeof(resultMsg), "Could not open %s for editing.", srcPath);
                }
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Regenerate"))
            {
                if (strcmp(archName, entry->name) == 0)
                {
                    // Prepend transform fields before user fields
                    static c8 rgTfNames[5][128] = {"PositionX", "PositionY", "PositionZ", "Rotation", "Scale"};
                    const u32 rgTfCount = 5;
                    const u32 rgTotal = rgTfCount + fieldCount;

                    FieldInfo rFields[ARCH_MAX_FIELDS + 5];
                    const c8 *rTypes[ARCH_MAX_FIELDS + 5];

                    rFields[0] = { rgTfNames[0], sizeof(f32), FIELD_TEMP_HOT }; rTypes[0] = "f32";
                    rFields[1] = { rgTfNames[1], sizeof(f32), FIELD_TEMP_HOT }; rTypes[1] = "f32";
                    rFields[2] = { rgTfNames[2], sizeof(f32), FIELD_TEMP_HOT }; rTypes[2] = "f32";
                    rFields[3] = { rgTfNames[3], sizeof(Vec4), FIELD_TEMP_HOT }; rTypes[3] = "Vec4";
                    if (archUniformScale) {
                        rFields[4] = { rgTfNames[4], sizeof(f32), FIELD_TEMP_HOT }; rTypes[4] = "f32";
                    } else {
                        rFields[4] = { rgTfNames[4], sizeof(Vec3), FIELD_TEMP_HOT }; rTypes[4] = "Vec3";
                    }
                    for (u32 i = 0; i < fieldCount; i++)
                    {
                        rFields[rgTfCount + i].name = fieldNames[i];
                        rFields[rgTfCount + i].size = typeSizes[fieldTypeIndex[i]];
                        rFields[rgTfCount + i].temperature = FIELD_TEMP_COLD;
                        rTypes[rgTfCount + i]       = typeNames[fieldTypeIndex[i]];
                    }
                    if (generateArchetypeFiles(hubProjectDir, archName, rFields, rTypes, rgTotal, archIsSingle, archIsBuffered, (u32)archBufferSize, archIsPhysicsBody, false))
                    {
                        entry->layout.count  = rgTotal;
                        entry->flags = 0;
                        if (archIsSingle && !archIsBuffered) FLAG_SET(entry->flags, ARCH_SINGLE);
                        if (archIsPersistent)  FLAG_SET(entry->flags, ARCH_PERSISTENT);
                        if (archUniformScale)  FLAG_SET(entry->flags, ARCH_FILE_UNIFORM_SCALE);
                        if (archIsBuffered)    FLAG_SET(entry->flags, ARCH_BUFFERED);
                        if (archIsPhysicsBody) FLAG_SET(entry->flags, ARCH_PHYSICS_BODY);
                        entry->poolCapacity  = (u32)archBufferSize;
                        snprintf(resultMsg, sizeof(resultMsg), "Regenerated %s files.", archName);
                    }
                    else
                        snprintf(resultMsg, sizeof(resultMsg), "Failed to regenerate %s!", archName);
                }
                else
                {
                    snprintf(resultMsg, sizeof(resultMsg),
                             "Click Edit on '%s' first, then Regenerate.", entry->name);
                }
            }

            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextDisabled("No archetypes registered yet.");
    }

    // ---- Hot / Cold Field Separation ----
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Hot / Cold Field Separation"))
    {
        // State persists across frames
        static i32  hcSelectedArch = -1;  // index into g_archRegistry
        // Per-field hot/cold flags — true = hot, false = cold
        // Indexed by [archIdx][fieldIdx], supports up to 32 archetypes × 32 fields
        static bool hcIsHot[MAX_ARCHETYPE_SYSTEMS][32];
        static bool hcInitDone[MAX_ARCHETYPE_SYSTEMS];

        const u32 CACHE_LINE = 64u; // bytes

        // Archetype selector combo
        ImGui::SetNextItemWidth(220.0f);
        const c8 *previewName = (hcSelectedArch >= 0 && hcSelectedArch < (i32)g_archRegistry.count)
                                ? g_archRegistry.entries[hcSelectedArch].name
                                : "(select archetype)";
        if (ImGui::BeginCombo("Archetype##hc", previewName))
        {
            for (u32 a = 0; a < g_archRegistry.count; a++)
            {
                bool sel = (hcSelectedArch == (i32)a);
                if (ImGui::Selectable(g_archRegistry.entries[a].name, sel))
                {
                    hcSelectedArch = (i32)a;
                    // Default: first 3 fields (Pos X/Y/Z) are hot, rest cold
                    if (!hcInitDone[a] && g_archRegistry.entries[a].layout.count > 0)
                    {
                        for (u32 f = 0; f < g_archRegistry.entries[a].layout.count && f < 32; f++)
                            hcIsHot[a][f] = (f < 3); // PositionX/Y/Z hot by default
                        hcInitDone[a] = true;
                    }
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (hcSelectedArch < 0 || hcSelectedArch >= (i32)g_archRegistry.count)
        {
            ImGui::TextDisabled("Select a registered archetype to analyse its hot/cold split.");
        }
        else
        {
            ArchetypeFileEntry *ae = &g_archRegistry.entries[hcSelectedArch];
            const u32 nFields = ae->layout.count < 32 ? ae->layout.count : 32;

            // Initialise defaults on first view of this archetype
            if (!hcInitDone[hcSelectedArch])
            {
                for (u32 f = 0; f < nFields; f++)
                    hcIsHot[hcSelectedArch][f] = (f < 3);
                hcInitDone[hcSelectedArch] = true;
            }

            // Field table with Hot/Cold radio toggles
            ImGui::Text("Tag each field as Hot (accessed every frame) or Cold (rarely accessed):");

            if (ImGui::BeginTable("##hcfields", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Field",  ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Size",   ImGuiTableColumnFlags_WidthFixed, 48.0f);
                ImGui::TableSetupColumn("Hot",    ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Cold",   ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableHeadersRow();

                for (u32 f = 0; f < nFields; f++)
                {
                    ImGui::TableNextRow();
                    ImGui::PushID((int)f + 5000);

                    const c8 *fname = ae->layout.fields ? ae->layout.fields[f].name : "?";
                    u32 fsize = ae->layout.fields ? ae->layout.fields[f].size : 0;

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(fname ? fname : "?");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%ub", fsize);

                    bool *isHot = &hcIsHot[hcSelectedArch][f];

                    ImGui::TableSetColumnIndex(2);
                    bool hotVal = *isHot;
                    if (ImGui::RadioButton("##hot", hotVal))
                        *isHot = true;

                    ImGui::TableSetColumnIndex(3);
                    bool coldVal = !(*isHot);
                    if (ImGui::RadioButton("##cold", coldVal))
                        *isHot = false;

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Separator();

            // Compute hot / cold sizes
            u32 hotBytes = 0, coldBytes = 0;
            for (u32 f = 0; f < nFields; f++)
            {
                u32 sz = ae->layout.fields ? ae->layout.fields[f].size : 0;
                if (hcIsHot[hcSelectedArch][f])
                    hotBytes += sz;
                else
                    coldBytes += sz;
            }
            u32 totalBytes = hotBytes + coldBytes;

            // Cache line analysis
            u32 hotCacheLines  = (hotBytes  + CACHE_LINE - 1) / CACHE_LINE;
            u32 coldCacheLines = (coldBytes + CACHE_LINE - 1) / CACHE_LINE;
            u32 mixedCacheLines = (totalBytes + CACHE_LINE - 1) / CACHE_LINE;

            // Memory layout summary
            ImGui::Text("Memory Layout (per entity)");

            // Hot strip
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.55f, 0.18f, 0.35f));
            ImGui::BeginChild("##hotblock", ImVec2(-1.0f, 28.0f), true);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            ImGui::Text("  HOT   %u bytes  →  %u cache line%s",
                        hotBytes, hotCacheLines, hotCacheLines == 1 ? "" : "s");
            ImGui::EndChild();
            ImGui::PopStyleColor();

            // Cold strip
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.35f, 0.65f, 0.35f));
            ImGui::BeginChild("##coldblock", ImVec2(-1.0f, 28.0f), true);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            ImGui::Text("  COLD  %u bytes  →  %u cache line%s",
                        coldBytes, coldCacheLines, coldCacheLines == 1 ? "" : "s");
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // Savings stat
            if (mixedCacheLines > 0)
            {
                ImGui::Text("Mixed layout (no split): %u cache lines per entity", mixedCacheLines);
                ImGui::Text("Hot-only iteration:       %u cache lines per entity  (%.0f%% fewer loads)",
                            hotCacheLines,
                            100.0f * (1.0f - (f32)hotCacheLines / (f32)mixedCacheLines));
            }

            ImGui::Spacing();

            // Batch size hint (how many entities fit in L1 data cache, typically 32 KB)
            const u32 L1_BYTES = 32768u;
            u32 hotBatch  = (hotBytes  > 0) ? (L1_BYTES / hotBytes)  : 0;
            u32 mixedBatch = (totalBytes > 0) ? (L1_BYTES / totalBytes) : 0;
            ImGui::TextDisabled("L1 cache (32 KB): fits ~%u entities hot-only vs ~%u entities mixed",
                                hotBatch, mixedBatch);

            ImGui::Spacing();
            ImGui::TextDisabled("To implement the split: define two archetypes (e.g. %sHot / %sCold)",
                                ae->name, ae->name);
            ImGui::TextDisabled("sharing the same entity index. Hot archetype holds per-frame fields;");
            ImGui::TextDisabled("cold holds model IDs, textures, and other rarely-touched data.");
        }
    }

    ImGui::End();
    #undef ARCH_MAX_FIELDS
}

// Ring buffer for frame time history
#define PROFILER_HISTORY_SIZE 120
static f32 frameTimeHistory[PROFILER_HISTORY_SIZE] = {0};
static f32 fpsHistory[PROFILER_HISTORY_SIZE] = {0};
static u32 profilerHistoryIndex = 0;
static f64 profilerAccumulator = 0.0;
static u32 profilerFrameCount = 0;
static f32 profilerAvgFps = 0.0f;
static f32 profilerAvgFrameTime = 0.0f;
static f32 profilerMinFrameTime = 9999.0f;
static f32 profilerMaxFrameTime = 0.0f;

static void drawProfilerWindow()
{
    if (!showProfiler) return;

    ImGui::Begin("Profiler", &showProfiler);

    f32 dt        = (f32)(1.0 / (editor->fps > 0.0 ? editor->fps : 1.0));
    f32 currentFps = (f32)editor->fps;
    f32 dtMs      = dt * 1000.0f;

    // ── Ring buffer update ──
    frameTimeHistory[profilerHistoryIndex] = dtMs;
    fpsHistory[profilerHistoryIndex]       = currentFps;
    profilerHistoryIndex = (profilerHistoryIndex + 1) % PROFILER_HISTORY_SIZE;

    profilerAccumulator += dt;
    profilerFrameCount++;
    if (profilerAccumulator >= 0.5f)
    {
        profilerAvgFps       = (f32)(profilerFrameCount / profilerAccumulator);
        profilerAvgFrameTime = (f32)(profilerAccumulator / profilerFrameCount) * 1000.0f;
        profilerAccumulator  = 0.0;
        profilerFrameCount   = 0;
    }

    profilerMinFrameTime = 9999.0f;
    profilerMaxFrameTime = 0.0f;
    for (u32 i = 0; i < PROFILER_HISTORY_SIZE; i++)
    {
        if (frameTimeHistory[i] > 0.0f)
        {
            if (frameTimeHistory[i] < profilerMinFrameTime) profilerMinFrameTime = frameTimeHistory[i];
            if (frameTimeHistory[i] > profilerMaxFrameTime) profilerMaxFrameTime = frameTimeHistory[i];
        }
    }

    // ── Header ──
    ImGui::Text("FPS: %.1f  (avg %.1f)", currentFps, profilerAvgFps);
    ImGui::Text("Frame: %.2f ms  |  min %.2f  max %.2f  avg %.2f",
                dtMs, profilerMinFrameTime, profilerMaxFrameTime, profilerAvgFrameTime);

    const ProfileFrame *prof = profileGetCurrentFrame();

    ImGui::Separator();

    // ── Frame time graph ──
    {
        f32 ordered[PROFILER_HISTORY_SIZE];
        for (u32 i = 0; i < PROFILER_HISTORY_SIZE; i++)
            ordered[i] = frameTimeHistory[(profilerHistoryIndex + i) % PROFILER_HISTORY_SIZE];
        c8 overlay[32];
        snprintf(overlay, sizeof(overlay), "%.2f ms", dtMs);
        ImGui::PlotLines("##FrameTime", ordered, PROFILER_HISTORY_SIZE, 0, overlay,
                         0.0f, profilerMaxFrameTime * 1.2f, ImVec2(0, 50));
    }

    ImGui::Separator();

    // ── Timing ──
    if (ImGui::CollapsingHeader("Timing", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (prof)
        {
            ImGui::Text("CPU Frame:  %.2f us  (%.3f ms)  [%llu cycles]",
                        (f32)prof->frameTime_us,
                        (f32)(prof->frameTime_us / 1000.0),
                        (unsigned long long)prof->frameCycles);
            ImGui::Text("GPU Frame:  %.2f us  (%.3f ms)",
                        (f32)prof->gpuFrameTime_us,
                        (f32)(prof->gpuFrameTime_us / 1000.0));
            f64 cpuGpuRatio = (prof->gpuFrameTime_us > 0.0)
                              ? prof->frameTime_us / prof->gpuFrameTime_us : 0.0;
            ImGui::Text("CPU/GPU ratio: %.2fx", (f32)cpuGpuRatio);
        }
        else
        {
            ImGui::TextDisabled("(no data)");
        }
    }

    ImGui::Separator();

    // ── Geometry ──
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (prof)
        {
            ImGui::Text("Draw Calls:   %u", prof->drawCalls);
            ImGui::Text("Triangles:    %u  (%s)", prof->triangles,
                        prof->triangles > 1000000 ? "HIGH" :
                        prof->triangles > 100000  ? "MED"  : "LOW");
            ImGui::Text("Vertices:     %u", prof->vertices);
            ImGui::Text("Prims Gen:    %llu", (unsigned long long)prof->primitivesGenerated);
            ImGui::Text("Entities:     %u", prof->entityCount);
            // triangles per draw call
            f32 triPerDC = prof->drawCalls > 0
                           ? (f32)prof->triangles / (f32)prof->drawCalls : 0.0f;
            ImGui::Text("Tri/Draw:     %.1f", triPerDC);
        }
        else
        {
            ImGui::TextDisabled("(no data)");
        }
    }

    ImGui::Separator();

    // ── GL State Changes ──
    if (ImGui::CollapsingHeader("GL State Changes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (prof)
        {
            if (ImGui::BeginTable("##glstate", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Counter", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Count",   ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                auto row = [](const char *label, u32 val)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%u", val);
                };

                row("Shader Binds",    prof->shaderBinds);
                row("Texture Binds",   prof->textureBinds);
                row("VAO Binds",       prof->vaoBinds);
                row("Buffer Binds",    prof->bufferBinds);
                row("Uniform Uploads", prof->uniformUploads);
                row("FBO Binds",       prof->fboBinds);

                ImGui::EndTable();
            }

            // buffer upload bandwidth
            f32 uploadKB = (f32)prof->bufferUploadBytes / 1024.0f;
            ImGui::Text("GPU Upload:  %u calls  %.1f KB/frame", prof->bufferUploadsCount, uploadKB);
        }
        else
        {
            ImGui::TextDisabled("(no data)");
        }
    }

    ImGui::Separator();

    // ── Memory ──
    if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (g_memory)
        {
            ImGui::Text("Total:    %.2f / %.2f MB",
                        (f32)memGetTotalUsed() / (1024.0f * 1024.0f),
                        (f32)memGetTotalSize() / (1024.0f * 1024.0f));
            ImGui::Text("Allocs:   %u", memGetAllocCount());

            // Per-arena progress bars
            for (u32 a = 0; a < MEM_ARENA_COUNT; a++)
            {
                u64 used = memGetArenaUsed((MemArenaID)a);
                u64 size = memGetArenaSize((MemArenaID)a);
                f32 frac = size > 0 ? (f32)used / (f32)size : 0.0f;
                c8 overlay[64];
                snprintf(overlay, sizeof(overlay), "%.1f / %.1f MB",
                         (f32)used / (1024.0f * 1024.0f),
                         (f32)size / (1024.0f * 1024.0f));
                ImGui::Text("%-10s", memGetArenaName((MemArenaID)a));
                ImGui::SameLine();
                ImGui::ProgressBar(frac, ImVec2(-1, 0), overlay);
            }

            if (ImGui::TreeNode("Per-Tag Breakdown"))
            {
                for (u32 t = 0; t < MEM_TAG_MAX; t++)
                {
                    u64 usage = memGetTagUsage((MemTag)t);
                    if (usage == 0) continue;
                    const c8 *name = memGetTagName((MemTag)t);
                    if (usage < 1024)
                        ImGui::Text("  %-14s %llu B", name, (unsigned long long)usage);
                    else if (usage < 1024 * 1024)
                        ImGui::Text("  %-14s %.1f KB", name, (f32)usage / 1024.0f);
                    else
                        ImGui::Text("  %-14s %.2f MB", name, (f32)usage / (1024.0f * 1024.0f));
                }
                ImGui::TreePop();
            }

            // Memory config editor (sizes for next launch)
            if (ImGui::TreeNode("Arena Config"))
            {
                static MemoryConfig editCfg = {0};
                static b8 loaded = false;
                if (!loaded)
                {
                    c8 cfgPath[512];
                    snprintf(cfgPath, sizeof(cfgPath), "%s/memory.conf", hubProjectDir);
                    if (!memLoadConfig(cfgPath, &editCfg))
                        editCfg = memDefaultConfig();
                    loaded = true;
                }

                ImGui::Text("Arena sizes (MB) — applied on next launch");
                ImGui::InputScalar("Total",    ImGuiDataType_U64, &editCfg.totalMB);
                ImGui::InputScalar("General",  ImGuiDataType_U64, &editCfg.arenaMB[MEM_ARENA_GENERAL]);
                ImGui::InputScalar("ECS",      ImGuiDataType_U64, &editCfg.arenaMB[MEM_ARENA_ECS]);
                ImGui::InputScalar("Renderer", ImGuiDataType_U64, &editCfg.arenaMB[MEM_ARENA_RENDERER]);
                ImGui::InputScalar("Physics",  ImGuiDataType_U64, &editCfg.arenaMB[MEM_ARENA_PHYSICS]);
                ImGui::InputScalar("Frame",    ImGuiDataType_U64, &editCfg.arenaMB[MEM_ARENA_FRAME]);

                if (ImGui::Button("Save Config"))
                {
                    c8 cfgPath[512];
                    snprintf(cfgPath, sizeof(cfgPath), "%s/memory.conf", hubProjectDir);
                    if (memSaveConfig(cfgPath, &editCfg))
                        INFO("Memory config saved to %s", cfgPath);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Defaults"))
                {
                    editCfg = memDefaultConfig();
                }
                ImGui::TreePop();
            }
        }
        else if (prof)
        {
            ImGui::Text("Allocs/frame:   %u  (%.1f KB)",
                        prof->heapAllocCount,
                        (f32)prof->heapAllocBytes / 1024.0f);
            ImGui::Text("Frees/frame:    %llu", (unsigned long long)prof->heapFreeCount);
            ImGui::Text("Live heap:      %.2f MB",
                        (f32)prof->heapLiveBytes / (1024.0f * 1024.0f));
        }
        else
        {
            ImGui::TextDisabled("(no data)");
        }
    }

    ImGui::Separator();

    // ── Per-scope breakdown ──
    if (ImGui::CollapsingHeader("Scope Breakdown", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (prof && prof->count > 0)
        {
            f64 totalUs = 0.0;
            for (u32 i = 0; i < prof->count; i++)
                totalUs += prof->entries[i].elapsed_us;

            if (ImGui::BeginTable("##scopes", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Scope",    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("us",       ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("cycles",   ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("%",        ImGuiTableColumnFlags_WidthFixed, 45.0f);
                ImGui::TableHeadersRow();

                for (u32 i = 0; i < prof->count; i++)
                {
                    const ProfileEntry *e = &prof->entries[i];
                    f32 pct = (totalUs > 0.0) ? (f32)(e->elapsed_us / totalUs * 100.0) : 0.0f;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e->name);
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.1f", (f32)e->elapsed_us);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%llu", (unsigned long long)e->cycles);
                    ImGui::TableSetColumnIndex(3);

                    if      (pct >= 50.0f) ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%.1f%%", pct);
                    else if (pct >= 20.0f) ImGui::TextColored(ImVec4(1,1,0.2f,1),    "%.1f%%", pct);
                    else                   ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "%.1f%%", pct);
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("TOTAL");
                ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%.1f", (f32)totalUs);
                ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("—");
                ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("100%%");

                ImGui::EndTable();
            }
        }
        else
        {
            ImGui::TextDisabled("(no data — build with -DDRUID_PROFILE)");
        }
    }

    ImGui::Separator();

    // ── Resources ──
    if (ImGui::CollapsingHeader("Resources"))
    {
        ImGui::Text("Entities: %u / %u  |  Viewport: %u x %u",
                    entityCount, entitySizeCache, viewportWidth, viewportHeight);
        if (resources)
        {
            ImGui::Text("Meshes %u/%u  Textures %u/%u  Shaders %u/%u",
                        resources->meshUsed,    resources->meshCount,
                        resources->textureUsed, resources->textureCount,
                        resources->shaderUsed,  resources->shaderCount);
            ImGui::Text("Materials %u/%u  Models %u/%u",
                        resources->materialUsed, resources->materialCount,
                        resources->modelUsed,    resources->modelCount);
        }
    }

    ImGui::End();
}
static Vec3 EulerAnglesDegrees = v3Zero;

// Helper: create a blank scene, resetting all editor state.
static void createBlankScene(u32 capacity)
{
    destroyArchetype(&sceneArchetype);

    entitySizeCache = capacity;
    entitySize      = (i32)capacity;
    entityCount     = 0;

    if (!createArchetype(&SceneEntity, entitySizeCache, &sceneArchetype))
    {
        FATAL("Failed to create new scene archetype");
        return;
    }

    rebindArchetypeFields();

    // Reset camera to default position
    sceneCam.pos         = {0.0f, 0.0f, 5.0f};
    sceneCam.orientation = quatIdentity();

    // Clear the current scene file path
    scenePathBuffer[0] = '\0';

    // Reset inspector
    inspectorEntityID    = 0;
    currentInspectorState = EMPTY_VIEW;
    manipulateTransform  = false;

    INFO("Created blank scene with capacity %u", entitySizeCache);
}

// Helper: load a scene file into the editor, replacing the current scene.
static void editorLoadSceneFile(const c8 *filePath)
{
    SceneData sd = loadScene(filePath);
    if (sd.archetypeCount > 0 && sd.archetypes)
    {
        sceneRemapModelIDs(&sd);

        destroyArchetype(&sceneArchetype);

        sceneArchetype  = sd.archetypes[0];
        dfree(sd.archetypes, sizeof(Archetype) * sd.archetypeCount, MEM_TAG_SCENE);
        sd.archetypes = nullptr;
        entitySizeCache = sceneArchetype.capacity;
        entityCount     = sceneArchetype.arena[0].count;
        entitySize      = (i32)entitySizeCache;

        migrateSceneArchetypeIfNeeded();
        rebindArchetypeFields();
        applySceneCameraEntityToSceneCam();

        if (sd.materialCount > 0 && sd.materials)
        {
            u32 count = sd.materialCount;
            if (count > resources->materialCount)
                count = resources->materialCount;
            memcpy(resources->materialBuffer, sd.materials,
                   sizeof(Material) * count);
            resources->materialUsed = count;
            dfree(sd.materials, sizeof(Material) * sd.materialCount, MEM_TAG_SCENE);
        }
        if (sd.modelRefs)
            dfree(sd.modelRefs, sizeof(c8[MAX_NAME_SIZE]) * sd.modelRefCount, MEM_TAG_SCENE);

        // Update the scene path buffer
        strncpy(scenePathBuffer, filePath, sizeof(scenePathBuffer) - 1);
        scenePathBuffer[sizeof(scenePathBuffer) - 1] = '\0';

        // Reset inspector
        inspectorEntityID    = 0;
        currentInspectorState = EMPTY_VIEW;
        manipulateTransform  = false;

        INFO("Loaded scene from %s (%u entities)", filePath, entityCount);
    }
    else
    {
        ERROR("Failed to load scene from %s", filePath);
    }
}

// Panel: list all .drsc files in the project scenes/ directory.
static void drawScenesPanel()
{
    if (!showScenesPanel) return;

    ImGui::Begin("Scenes", &showScenesPanel);

    // Rescan when the panel first opens or when user requests
    static c8 **sceneFiles = nullptr;
    static u32 sceneFileCount = 0;
    static b8 needsRescan = true;
    static i32 selectedIndex = -1;

    if (ImGui::Button("Refresh"))
        needsRescan = true;

    if (needsRescan)
    {
        // Free previous scan
        if (sceneFiles)
        {
            for (u32 i = 0; i < sceneFileCount; i++)
                free(sceneFiles[i]);
            free(sceneFiles);
            sceneFiles = nullptr;
        }
        sceneFileCount = 0;
        selectedIndex = -1;

        c8 scenesDir[512];
        snprintf(scenesDir, sizeof(scenesDir), "%s/scenes", hubProjectDir);

        u32 totalFiles = 0;
        c8 **allFiles = listFilesInDirectory(scenesDir, &totalFiles);
        if (allFiles && totalFiles > 0)
        {
            sceneFiles = (c8 **)malloc(sizeof(c8 *) * totalFiles);
            for (u32 i = 0; i < totalFiles; i++)
            {
                u32 len = (u32)strlen(allFiles[i]);
                if (len > 5 && strcmp(allFiles[i] + len - 5, ".drsc") == 0)
                {
                    sceneFiles[sceneFileCount] = allFiles[i];
                    sceneFileCount++;
                }
                else
                {
                    free(allFiles[i]);
                }
            }
            free(allFiles);
        }
        needsRescan = false;
    }

    ImGui::Separator();

    // List .drsc files
    if (sceneFileCount == 0)
    {
        ImGui::TextDisabled("No .drsc files in scenes/");
    }
    else
    {
        for (u32 i = 0; i < sceneFileCount; i++)
        {
            // Show just the filename
            const c8 *name = sceneFiles[i];
            const c8 *slash = strrchr(name, '/');
            if (!slash) slash = strrchr(name, '\\');
            const c8 *display = slash ? slash + 1 : name;

            // Highlight the currently loaded scene
            b8 isCurrent = (scenePathBuffer[0] != '\0'
                            && strcmp(sceneFiles[i], scenePathBuffer) == 0);

            if (isCurrent)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));

            b8 isSelected = ((i32)i == selectedIndex);
            if (ImGui::Selectable(display, isSelected))
                selectedIndex = (i32)i;

            // Double-click to load
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                editorLoadSceneFile(sceneFiles[i]);
                needsRescan = true;
            }

            if (isCurrent)
                ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();

    // Action buttons
    b8 hasSelection = (selectedIndex >= 0 && (u32)selectedIndex < sceneFileCount);

    if (ImGui::Button("Load") && hasSelection)
    {
        editorLoadSceneFile(sceneFiles[selectedIndex]);
        needsRescan = true;
    }
    ImGui::SameLine();

    if (ImGui::Button("New Scene"))
    {
        createBlankScene(128);
        needsRescan = true;
    }
    ImGui::SameLine();

    if (ImGui::Button("Set as Startup") && hasSelection)
    {
        // Extract just the filename from the full path
        const c8 *name = sceneFiles[selectedIndex];
        const c8 *slash = strrchr(name, '/');
        if (!slash) slash = strrchr(name, '\\');
        const c8 *filename = slash ? slash + 1 : name;

        c8 cfgPath[512];
        snprintf(cfgPath, sizeof(cfgPath), "%s/startup_scene.txt", hubProjectDir);
        FILE *cfg = fopen(cfgPath, "wb");
        if (cfg)
        {
            fwrite(filename, 1, strlen(filename), cfg);
            fclose(cfg);
            INFO("Set startup scene to '%s'", filename);
        }
        else
        {
            ERROR("Failed to write startup_scene.txt");
        }
    }

    ImGui::End();
}

static void drawSceneListWindow()
{
    ImGui::Begin("Scene List");
    ImGui::InputInt("Max Entity Size", &entitySize);

    if (ImGui::Button("Add Entity"))
    {
        // Add bounds checking for entity creation
        if (entityCount >= entitySizeCache)
        {
            WARN("Cannot add entity: reached maximum entity count (%u)", entitySizeCache);
        }
        else
        {
            isActive[entityCount] = true;
            scales[entityCount] = {1, 1, 1};
            positions[entityCount] = {0, 0, 0};
            rotations[entityCount] = quatIdentity();
            modelIDs[entityCount] = (u32)-1;
            entityMaterialIDs[entityCount] = (u32)-1;
            shaderHandles[entityCount] = 0;
            archetypeIDs[entityCount] = (u32)-1;
            if (ecsSlotIDs)       ecsSlotIDs[entityCount]       = (u32)-1;
            if (sceneCameraFlags) sceneCameraFlags[entityCount]  = false;
            if (entityTags)       entityTags[entityCount * 32]   = '\0';
            if (isLight)          isLight[entityCount]           = false;

            sprintf(&names[entityCount * MAX_NAME_SIZE], "Entity %d", entityCount);

            entityCount++;
            sceneArchetype.arena[0].count = entityCount;
            if (sceneArchetype.activeChunkCount == 0)
                sceneArchetype.activeChunkCount = 1;
            DEBUG("Added Entity %d\n", entityCount);
        }
    }

    // Tag filter
    static c8 tagFilter[32] = "";
    if (entityTags)
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##TagFilter", "Filter by tag...", tagFilter, sizeof(tagFilter));
    }

    ImGui::Separator();

    c8 *entityName;
    for (u32 i = 0; i < entityCount; i++)
    {
        // Filter by tag if a filter is active
        if (entityTags && tagFilter[0] != '\0')
        {
            c8 *tag = &entityTags[i * 32];
            if (tag[0] == '\0') continue; // skip untagged entities when filtering
            // case-insensitive substring match
            b8 match = false;
            for (u32 t = 0; tag[t] && !match; t++)
            {
                b8 sub = true;
                for (u32 f = 0; tagFilter[f] && sub; f++)
                {
                    c8 a = tag[t + f]; c8 b = tagFilter[f];
                    if (a >= 'A' && a <= 'Z') a += 32;
                    if (b >= 'A' && b <= 'Z') b += 32;
                    if (a != b) sub = false;
                }
                if (sub) match = true;
            }
            if (!match) continue;
        }

        entityName = &names[i * MAX_NAME_SIZE];

        ImGui::PushID(i);
        static c8 buttonLabel[320];
        const c8 *baseLabel = (entityName[0] == '\0') ? "[Unnamed Entity]" : entityName;

        // Build label with tag prefix
        c8 tagPrefix[40] = "";
        if (entityTags && entityTags[i * 32] != '\0')
            snprintf(tagPrefix, sizeof(tagPrefix), "[%s] ", &entityTags[i * 32]);

        if (sceneCameraFlags && sceneCameraFlags[i])
            snprintf(buttonLabel, sizeof(buttonLabel), "%s[Camera] %s", tagPrefix, baseLabel);
        else if (isLight && isLight[i])
            snprintf(buttonLabel, sizeof(buttonLabel), "%s[Light] %s", tagPrefix, baseLabel);
        else
            snprintf(buttonLabel, sizeof(buttonLabel), "%s%s", tagPrefix, baseLabel);

        // draw list of entities
        if (ImGui::Button(buttonLabel))
        {
            // tell the inspector what to read
            currentInspectorState = ENTITY_VIEW;
            inspectorEntityID = i;

            Vec3 eulerRadians = eulerFromQuat(rotations[inspectorEntityID]);
            EulerAnglesDegrees.x = degrees(eulerRadians.x);
            EulerAnglesDegrees.y = degrees(eulerRadians.y);
            EulerAnglesDegrees.z = degrees(eulerRadians.z);
        }
        ImGui::PopID();
    }
    ImGui::End();
}

// Render an editable widget for a single SoA field of the selected entity.
// Uses field size + name heuristics to pick the right ImGui widget.
static void drawSoAField(const c8 *fieldName, u32 fieldSize, void *ptr)
{
    if (fieldSize == sizeof(b8))
    {
        bool v = *(b8 *)ptr;
        if (ImGui::Checkbox(fieldName, &v))
            *(b8 *)ptr = (b8)v;
        return;
    }

    if (fieldSize >= 64)  // c8[] string buffer
    {
        ImGui::InputText(fieldName, (c8 *)ptr, (size_t)fieldSize);
        return;
    }

    if (fieldSize == sizeof(Vec4))
    {
        ImGui::DragFloat4(fieldName, (f32 *)ptr, 0.001f);
        return;
    }

    if (fieldSize == sizeof(Vec3))
    {
        ImGui::DragFloat3(fieldName, (f32 *)ptr, 0.01f);
        return;
    }

    if (fieldSize == sizeof(Vec2))
    {
        ImGui::DragFloat2(fieldName, (f32 *)ptr, 0.01f);
        return;
    }

    if (fieldSize == sizeof(u32))
    {
        // Heuristic: name ends in "ID", "id", "Handle", "Index", "Count" → treat as uint
        u32 nameLen = (u32)strlen(fieldName);
        bool isUint = (nameLen >= 2 && strcmp(fieldName + nameLen - 2, "ID") == 0)
                   || (nameLen >= 2 && strcmp(fieldName + nameLen - 2, "id") == 0)
                   || (nameLen >= 5 && strcmp(fieldName + nameLen - 5, "Index") == 0)
                   || (nameLen >= 5 && strcmp(fieldName + nameLen - 5, "Count") == 0)
                   || (nameLen >= 6 && strcmp(fieldName + nameLen - 6, "Handle") == 0)
                   || strstr(fieldName, "Shader") != nullptr;
        if (isUint)
        {
            i32 v = (i32)*(u32 *)ptr;
            if (ImGui::InputInt(fieldName, &v))
                *(u32 *)ptr = (u32)(v < 0 ? 0 : v);
        }
        else
        {
            ImGui::DragFloat(fieldName, (f32 *)ptr, 0.01f);
        }
        return;
    }

    // Fallback — show as hex dump
    ImGui::TextDisabled("%s  (%u bytes)", fieldName, fieldSize);
}

// Collapsible section showing all SoA fields of the sceneArchetype for one entity.
static void drawSoAEditorSection(u32 entityID)
{
    void **fields = getArchetypeFields(&sceneArchetype, 0);
    StructLayout *layout = sceneArchetype.layout;
    if (!fields || !layout || entityID >= sceneArchetype.arena[0].count)
        return;

    if (!ImGui::CollapsingHeader("SoA Raw Fields"))
        return;

    ImGui::PushID((int)entityID + 10000);

    for (u32 f = 0; f < layout->count; f++)
    {
        const c8 *fname = layout->fields[f].name;
        u32       fsize = layout->fields[f].size;
        void     *ptr   = (u8 *)fields[f] + (u64)fsize * entityID;

        ImGui::PushID((int)f);
        drawSoAField(fname, fsize, ptr);
        ImGui::PopID();
    }

    ImGui::PopID();
}

static void drawInspectorWindow()
{
    ImGui::Begin("Inspector");
    switch (currentInspectorState)
    {
    default:
    case InspectorState::EMPTY_VIEW:
        ImGui::Spacing();
        ImGui::TextDisabled("Select an entity in the viewport or scene list.");
        break;
    case InspectorState::SKYBOX_VIEW:
        ImGui::Text("Skybox Settings");
        ImGui::Separator();
        if (ImGui::Button("Edit Skybox Textures"))
            showSkyboxSettings = true;
        break;
    case InspectorState::ENTITY_VIEW:
        if (!positions || !rotations || !scales || !isActive || !names
            || !modelIDs || inspectorEntityID >= entityCount) {
            ImGui::TextDisabled("No entity selected or scene not loaded.");
            break;
        }

        // Sync euler angles whenever a different entity is selected
        {
            static u32 lastInspectorID = (u32)-1;
            if (inspectorEntityID != lastInspectorID)
            {
                lastInspectorID = inspectorEntityID;
                Vec3 r = eulerFromQuat(rotations[inspectorEntityID]);
                EulerAnglesDegrees.x = degrees(r.x);
                EulerAnglesDegrees.y = degrees(r.y);
                EulerAnglesDegrees.z = degrees(r.z);
            }
        }

        // ---- Header row: name | active | delete ----
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
        ImGui::InputText("##Name", &names[inspectorEntityID * MAX_NAME_SIZE], MAX_NAME_SIZE);
        ImGui::SameLine();
        if (isActive)
        {
            bool active = (bool)isActive[inspectorEntityID];
            if (ImGui::Checkbox("##Active", &active))
                isActive[inspectorEntityID] = (b8)active;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Active");
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("Delete"))
        {
            u32 rem  = inspectorEntityID;
            u32 last = entityCount - 1;
            if (rem != last)
            {
                void **fields = getArchetypeFields(&sceneArchetype, 0);
                u32 fc = sceneArchetype.layout->count;
                for (u32 f = 0; f < fc; f++)
                {
                    u32 sz = sceneArchetype.layout->fields[f].size;
                    u8 *base = (u8 *)fields[f];
                    memcpy(base + rem * sz, base + last * sz, sz);
                }
            }
            entityCount--;
            sceneArchetype.arena[0].count = entityCount;
            if (entityCount > 0 && inspectorEntityID >= entityCount)
                inspectorEntityID = entityCount - 1;
            else if (entityCount == 0)
                currentInspectorState = EMPTY_VIEW;
        }
        ImGui::PopStyleColor();

        // ---- Transform ----
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat3("Position##tr", (f32 *)&positions[inspectorEntityID], 0.01f);
            if (ImGui::DragFloat3("Rotation##tr", (f32 *)&EulerAnglesDegrees, 0.5f))
            {
                Vec3 eulerRadians = { radians(EulerAnglesDegrees.x),
                                     radians(EulerAnglesDegrees.y),
                                     radians(EulerAnglesDegrees.z) };
                rotations[inspectorEntityID] = quatFromEuler(eulerRadians);
            }
            ImGui::DragFloat3("Scale##tr", (f32 *)&scales[inspectorEntityID], 0.01f);
            if (entityTags)
                ImGui::InputText("Tag##tr", &entityTags[inspectorEntityID * 32], 32);
        }

        // ---- Rendering ----
        if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
            u32 currentModelID = modelIDs[inspectorEntityID];

            // Model combo
            const c8 *modelPreview = (currentModelID == (u32)-1) ? "None"
                : (currentModelID < resources->modelUsed ? resources->modelBuffer[currentModelID].name : "Invalid");
            if (ImGui::BeginCombo("Model##rend", modelPreview))
            {
                if (ImGui::Selectable("None", currentModelID == (u32)-1))
                    modelIDs[inspectorEntityID] = (u32)-1;
                for (u32 mi = 0; mi < resources->modelUsed; mi++)
                {
                    ImGui::PushID((int)mi);
                    bool sel = (currentModelID == mi);
                    if (ImGui::Selectable(resources->modelBuffer[mi].name, sel))
                        modelIDs[inspectorEntityID] = mi;
                    if (sel) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            // Shader combo
            if (shaderHandles)
            {
                const c8 *currentShaderName = "None";
                for (u32 si = 0; si < resources->shaderIDs.capacity; si++)
                {
                    if (!resources->shaderIDs.pairs[si].occupied) continue;
                    u32 idx = *(u32 *)resources->shaderIDs.pairs[si].value;
                    if (resources->shaderHandles[idx] == shaderHandles[inspectorEntityID])
                    { currentShaderName = (const c8 *)resources->shaderIDs.pairs[si].key; break; }
                }
                if (ImGui::BeginCombo("Shader##rend", currentShaderName))
                {
                    for (u32 si = 0; si < resources->shaderIDs.capacity; si++)
                    {
                        if (!resources->shaderIDs.pairs[si].occupied) continue;
                        const c8 *sname  = (const c8 *)resources->shaderIDs.pairs[si].key;
                        u32 idx          = *(u32 *)resources->shaderIDs.pairs[si].value;
                        u32 shandle      = resources->shaderHandles[idx];
                        bool isSel       = (shaderHandles[inspectorEntityID] == shandle);
                        if (ImGui::Selectable(sname, isSel))
                            shaderHandles[inspectorEntityID] = shandle;
                        if (isSel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            // Model stats (collapsed by default — useful but not primary)
            currentModelID = modelIDs[inspectorEntityID];
            if (currentModelID < resources->modelUsed && ImGui::TreeNode("Model Info"))
            {
                Model *infoModel = &resources->modelBuffer[currentModelID];
                ImGui::Text("ID %u  |  Meshes %u  |  Materials %u",
                            currentModelID, infoModel->meshCount, infoModel->materialCount);
                u32 totalTris = 0;
                for (u32 i = 0; i < infoModel->meshCount; i++)
                {
                    u32 mi = infoModel->meshIndices[i];
                    if (mi < resources->meshUsed) totalTris += resources->meshBuffer[mi].drawCount / 3;
                }
                ImGui::Text("Triangles: %u", totalTris);
                if (ImGui::TreeNode("Mesh Breakdown"))
                {
                    for (u32 i = 0; i < infoModel->meshCount; i++)
                    {
                        u32 mi  = infoModel->meshIndices[i];
                        u32 mat = (i < infoModel->materialCount) ? infoModel->materialIndices[i] : (u32)-1;
                        if (mi >= resources->meshUsed) { ImGui::TextDisabled("Mesh %u: invalid", i); continue; }
                        u32 tris = resources->meshBuffer[mi].drawCount / 3;
                        if (mat == (u32)-1) ImGui::Text("Mesh %u: %u tris, no mat", i, tris);
                        else                ImGui::Text("Mesh %u: %u tris, mat %u", i, tris, mat);
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }

        // ---- Material ----
        if (ImGui::CollapsingHeader("Material###MatSection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            u32 modelID = modelIDs[inspectorEntityID];
            u32 defaultMaterialIndex = (u32)-1;
            if (modelID < resources->modelUsed)
            {
                Model *model = &resources->modelBuffer[modelID];
                if (model->meshCount > 0)
                    defaultMaterialIndex = model->materialIndices[0];
            }

            // Current effective material
            u32 assignedID = entityMaterialIDs ? entityMaterialIDs[inspectorEntityID] : (u32)-1;
            u32 effectiveIdx = (assignedID != (u32)-1) ? assignedID : defaultMaterialIndex;

            // Find current material name for the combo preview
            const c8 *comboPreview = (effectiveIdx == defaultMaterialIndex && assignedID == (u32)-1)
                                   ? "Default" : "(none)";
            if (effectiveIdx < resources->materialUsed)
            {
                for (u32 i = 0; i < resources->materialIDs.capacity; i++)
                {
                    if (!resources->materialIDs.pairs[i].occupied) continue;
                    if (*(u32 *)resources->materialIDs.pairs[i].value == effectiveIdx)
                    { comboPreview = (const c8 *)resources->materialIDs.pairs[i].key; break; }
                }
            }

            // Material assignment dropdown
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
            if (ImGui::BeginCombo("##matAssign", comboPreview))
            {
                // Default option (use model's own material)
                bool defSel = (assignedID == (u32)-1);
                if (ImGui::Selectable("Default (model)", defSel))
                {
                    if (entityMaterialIDs) entityMaterialIDs[inspectorEntityID] = (u32)-1;
                }
                if (defSel) ImGui::SetItemDefaultFocus();
                ImGui::Separator();
                // All named materials from registry
                for (u32 i = 0; i < resources->materialIDs.capacity; i++)
                {
                    if (!resources->materialIDs.pairs[i].occupied) continue;
                    u32 idx = *(u32 *)resources->materialIDs.pairs[i].value;
                    if (idx >= resources->materialUsed) continue;
                    const c8 *mname = (const c8 *)resources->materialIDs.pairs[i].key;
                    bool sel = (assignedID == idx);
                    ImGui::PushID((int)idx);
                    if (ImGui::Selectable(mname, sel))
                    {
                        if (entityMaterialIDs) entityMaterialIDs[inspectorEntityID] = idx;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Edit##matEdit"))
            {
                if (effectiveIdx < resources->materialUsed)
                {
                    g_selectedMaterialIdx = (i32)effectiveIdx;
                    showResourceManager   = true;
                    g_resourceManagerTab  = 3;
                }
            }

            // Read-only summary of the active material
            if (effectiveIdx < resources->materialUsed)
            {
                Material *mat = &resources->materialBuffer[effectiveIdx];
                ImGui::Spacing();
                ImGui::BeginDisabled(true);
                f32 col[3] = { mat->colour.x, mat->colour.y, mat->colour.z };
                ImGui::ColorEdit3("Colour##matRO",        col);
                ImGui::SliderFloat("Metallic##matRO",     &mat->metallic,      0.0f, 1.0f);
                ImGui::SliderFloat("Roughness##matRO",    &mat->roughness,     0.0f, 1.0f);
                ImGui::SliderFloat("Emissive##matRO",     &mat->emissive,      0.0f, 10.0f);
                ImGui::SliderFloat("Transparency##matRO", &mat->transparency,  0.0f, 1.0f);
                ImGui::EndDisabled();
                ImGui::TextDisabled("Open registry to edit textures and values.");
            }
        }

        // ---- Light ----
        if (isLight)
        {
            u32 eid = inspectorEntityID;
            bool lightEnabled = (bool)isLight[eid];
            if (ImGui::Checkbox("Light Source##lgt", &lightEnabled))
            {
                isLight[eid] = (b8)lightEnabled;
                if (lightEnabled)
                {
                    if (lightTypes)       lightTypes[eid]       = LIGHT_TYPE_POINT;
                    if (lightColorRs)     lightColorRs[eid]     = 1.0f;
                    if (lightColorGs)     lightColorGs[eid]     = 1.0f;
                    if (lightColorBs)     lightColorBs[eid]     = 1.0f;
                    if (lightIntensities) lightIntensities[eid] = 1.0f;
                    if (lightRanges)      lightRanges[eid]      = 10.0f;
                    if (lightDirXs)       lightDirXs[eid]       = 0.0f;
                    if (lightDirYs)       lightDirYs[eid]       = -1.0f;
                    if (lightDirZs)       lightDirZs[eid]       = 0.0f;
                    if (lightInnerCones)  lightInnerCones[eid]  = 0.9063f;
                    if (lightOuterCones)  lightOuterCones[eid]  = 0.8192f;
                }
            }

            if (isLight[eid] && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static const c8 *lightTypeNames[] = { "Point", "Directional", "Spot" };
                if (lightTypes)
                {
                    int lt = (int)lightTypes[eid];
                    if (lt < 0 || lt > 2) lt = 0;
                    if (ImGui::Combo("Type##lgt", &lt, lightTypeNames, 3))
                        lightTypes[eid] = (u32)lt;
                }
                if (lightColorRs && lightColorGs && lightColorBs)
                {
                    f32 col[3] = { lightColorRs[eid], lightColorGs[eid], lightColorBs[eid] };
                    if (ImGui::ColorEdit3("Color##lgt", col))
                    { lightColorRs[eid] = col[0]; lightColorGs[eid] = col[1]; lightColorBs[eid] = col[2]; }
                }
                if (lightIntensities)
                    ImGui::DragFloat("Intensity##lgt", &lightIntensities[eid], 0.05f, 0.0f, 100.0f, "%.2f");

                u32 lt = lightTypes ? lightTypes[eid] : 0;
                if (lt != LIGHT_TYPE_DIRECTIONAL && lightRanges)
                    ImGui::DragFloat("Range##lgt", &lightRanges[eid], 0.1f, 0.1f, 1000.0f, "%.1f m");
                if ((lt == LIGHT_TYPE_DIRECTIONAL || lt == LIGHT_TYPE_SPOT) && lightDirXs && lightDirYs && lightDirZs)
                {
                    f32 dir[3] = { lightDirXs[eid], lightDirYs[eid], lightDirZs[eid] };
                    if (ImGui::DragFloat3("Direction##lgt", dir, 0.01f, -1.0f, 1.0f, "%.3f"))
                    { lightDirXs[eid] = dir[0]; lightDirYs[eid] = dir[1]; lightDirZs[eid] = dir[2]; }
                }
                if (lt == LIGHT_TYPE_SPOT)
                {
                    if (lightInnerCones)
                        ImGui::DragFloat("Inner Cone##lgt", &lightInnerCones[eid], 0.005f, 0.0f, 1.0f, "%.4f (cos)");
                    if (lightOuterCones)
                        ImGui::DragFloat("Outer Cone##lgt", &lightOuterCones[eid], 0.005f, 0.0f, 1.0f, "%.4f (cos)");
                }
            }
        }

        // ---- Physics Body ----
        if (archetypeIDs)
        {
            u32 archID = archetypeIDs[inspectorEntityID];
            if (archID < g_archRegistry.count
                && FLAG_CHECK(g_archRegistry.entries[archID].flags, ARCH_PHYSICS_BODY))
            {
                if (ImGui::CollapsingHeader("Physics Body", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    u32 eid = inspectorEntityID;
                    ImGui::SeparatorText("Body");
                    static const c8 *bodyTypeNames[] = { "Static", "Dynamic", "Kinematic" };
                    if (physicsBodyTypes)
                    {
                        int bt = (int)physicsBodyTypes[eid];
                        if (bt < 0 || bt > 2) bt = 0;
                        if (ImGui::Combo("Body Type##phys", &bt, bodyTypeNames, 3))
                            physicsBodyTypes[eid] = (u32)bt;
                        if (bt == 0)      ImGui::TextDisabled("  Static — not affected by forces or gravity");
                        else if (bt == 1) ImGui::TextDisabled("  Dynamic — simulated (needs mass > 0)");
                        else              ImGui::TextDisabled("  Kinematic — moved by code, collides with dynamic");
                    }
                    if (masses)
                    {
                        ImGui::DragFloat("Mass (kg)##phys", &masses[eid], 0.5f, 0.0f, 100000.0f, "%.1f kg");
                        if (masses[eid] < 0.0f) masses[eid] = 0.0f;
                        if (masses[eid] == 0.0f) ImGui::TextDisabled("  Mass 0 = infinite (immovable)");
                    }

                    ImGui::SeparatorText("Collider");
                    static const c8 *shapeNames[] = { "None", "Sphere", "Box" };
                    static bool colliderMatchTransform[4096] = {false};
                    if (colliderShapes)
                    {
                        int cs = (int)colliderShapes[eid];
                        if (cs < 0 || cs > 2) cs = 0;
                        if (ImGui::Combo("Shape##phys", &cs, shapeNames, 3))
                            colliderShapes[eid] = (u32)cs;
                        if (cs > 0 && eid < 4096)
                        {
                            ImGui::Checkbox("Match Transform##phys", &colliderMatchTransform[eid]);
                            if (colliderMatchTransform[eid])
                                ImGui::TextDisabled("  Auto-syncs to entity scale");
                        }
                        if (cs > 0 && eid < 4096 && colliderMatchTransform[eid])
                        {
                            Vec3 s = scales[eid];
                            if (cs == 1 && sphereRadii)
                            {
                                f32 r = s.x; if (s.y > r) r = s.y; if (s.z > r) r = s.z;
                                sphereRadii[eid] = r;
                            }
                            else if (cs == 2 && colliderHalfXs && colliderHalfYs && colliderHalfZs)
                            { colliderHalfXs[eid] = s.x; colliderHalfYs[eid] = s.y; colliderHalfZs[eid] = s.z; }
                        }
                        if (cs == 1 && sphereRadii)
                        {
                            if (eid < 4096 && colliderMatchTransform[eid]) ImGui::BeginDisabled();
                            ImGui::DragFloat("Radius##phys", &sphereRadii[eid], 0.05f, 0.01f, 500.0f, "%.2f m");
                            if (eid < 4096 && colliderMatchTransform[eid]) ImGui::EndDisabled();
                        }
                        else if (cs == 2 && colliderHalfXs && colliderHalfYs && colliderHalfZs)
                        {
                            if (eid < 4096 && colliderMatchTransform[eid]) ImGui::BeginDisabled();
                            f32 he[3] = { colliderHalfXs[eid], colliderHalfYs[eid], colliderHalfZs[eid] };
                            if (ImGui::DragFloat3("Half Extents##phys", he, 0.05f, 0.01f, 500.0f, "%.2f m"))
                            { colliderHalfXs[eid] = he[0]; colliderHalfYs[eid] = he[1]; colliderHalfZs[eid] = he[2]; }
                            ImGui::TextDisabled("  Full size  %.2f x %.2f x %.2f",
                                                he[0]*2.f, he[1]*2.f, he[2]*2.f);
                            if (eid < 4096 && colliderMatchTransform[eid]) ImGui::EndDisabled();
                        }
                        if (cs == 0) ImGui::TextDisabled("  No collision shape");
                    }

                    bool inPlay = g_ecsSystemLoaded[archID] && g_ecsArchetypes[archID].arena;
                    if (inPlay)
                    {
                        u32 slot = ecsSlotIDs ? ecsSlotIDs[eid] : (u32)-1;
                        Archetype *physArch = &g_ecsArchetypes[archID];
                        void **physFields = getArchetypeFields(physArch, 0);
                        if (physFields && slot != (u32)-1 && slot < physArch->arena[0].count)
                        {
                            ImGui::SeparatorText("Runtime");
                            StructLayout *physLayout = physArch->layout;
                            i32 dampIdx=-1,velXIdx=-1,velYIdx=-1,velZIdx=-1,fxIdx=-1,fyIdx=-1,fzIdx=-1;
                            for (u32 f = 0; f < physLayout->count; f++)
                            {
                                const c8 *n = physLayout->fields[f].name;
                                if      (!strcmp(n,"LinearDamping"))   dampIdx=(i32)f;
                                else if (!strcmp(n,"LinearVelocityX")) velXIdx=(i32)f;
                                else if (!strcmp(n,"LinearVelocityY")) velYIdx=(i32)f;
                                else if (!strcmp(n,"LinearVelocityZ")) velZIdx=(i32)f;
                                else if (!strcmp(n,"ForceX"))          fxIdx=(i32)f;
                                else if (!strcmp(n,"ForceY"))          fyIdx=(i32)f;
                                else if (!strcmp(n,"ForceZ"))          fzIdx=(i32)f;
                            }
                            if (dampIdx >= 0)
                                ImGui::DragFloat("Damping##rt", &((f32*)physFields[dampIdx])[slot], 0.001f, 0.0f, 1.0f, "%.3f");
                            if (velXIdx>=0 && velYIdx>=0 && velZIdx>=0)
                            {
                                f32 vx=((f32*)physFields[velXIdx])[slot], vy=((f32*)physFields[velYIdx])[slot], vz=((f32*)physFields[velZIdx])[slot];
                                ImGui::Text("Velocity  %.2f  %.2f  %.2f  (%.2f m/s)", vx, vy, vz, sqrtf(vx*vx+vy*vy+vz*vz));
                            }
                            if (fxIdx>=0 && fyIdx>=0 && fzIdx>=0)
                                ImGui::Text("Force     %.2f  %.2f  %.2f",
                                    ((f32*)physFields[fxIdx])[slot], ((f32*)physFields[fyIdx])[slot], ((f32*)physFields[fzIdx])[slot]);
                        }
                    }
                    else
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Velocity / damping visible in play mode");
                    }
                }
            }
        }

        // ---- Scene Camera ----
        if (sceneCameraFlags)
        {
            if (ImGui::CollapsingHeader("Scene Camera"))
            {
                b8 isSceneCamera = sceneCameraFlags[inspectorEntityID];
                if (ImGui::Checkbox("Use as Scene Camera##sc", (bool *)&isSceneCamera))
                {
                    if (isSceneCamera) { setSceneCameraEntity(inspectorEntityID); applySceneCameraEntityToSceneCam(); }
                    else sceneCameraFlags[inspectorEntityID] = false;
                }
                if (sceneCameraFlags[inspectorEntityID])
                {
                    if (ImGui::Button("Move View To Camera##sc")) applySceneCameraEntityToSceneCam();
                    ImGui::SameLine();
                    if (ImGui::Button("Match Camera To View##sc"))
                    { positions[inspectorEntityID] = sceneCam.pos; rotations[inspectorEntityID] = sceneCam.orientation; }
                }
            }
        }

        // ---- Archetype ----
        if (archetypeIDs)
        {
            u32 currentArchID = archetypeIDs[inspectorEntityID];
            const c8 *archPreview = (currentArchID < g_archRegistry.count)
                ? g_archRegistry.entries[currentArchID].name : "None";

            if (ImGui::CollapsingHeader("Archetype", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginCombo("##archCombo", archPreview))
                {
                    if (ImGui::Selectable("None", currentArchID == (u32)-1))
                        archetypeIDs[inspectorEntityID] = (u32)-1;
                    for (u32 a = 0; a < g_archRegistry.count; a++)
                    {
                        bool sel = (currentArchID == a);
                        if (ImGui::Selectable(g_archRegistry.entries[a].name, sel))
                            archetypeIDs[inspectorEntityID] = a;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (currentArchID < g_archRegistry.count)
                {
                    ArchetypeFileEntry *archEntry = &g_archRegistry.entries[currentArchID];
                    if (g_gameRunning && g_ecsSystemLoaded[currentArchID]
                        && g_ecsArchEntityCount[currentArchID] > 0
                        && g_ecsArchetypes[currentArchID].arena)
                    {
                        u32 perSysIdx = (u32)-1;
                        for (u32 i = 0; i < g_ecsArchEntityCount[currentArchID]; i++)
                        {
                            if (g_ecsSceneMap[currentArchID][i] == inspectorEntityID)
                            { perSysIdx = i; break; }
                        }
                        if (perSysIdx != (u32)-1)
                        {
                            void **fields = getArchetypeFields(&g_ecsArchetypes[currentArchID], 0);
                            StructLayout *layout = g_ecsArchetypes[currentArchID].layout;
                            if (fields && layout)
                            {
                                for (u32 f = 0; f < layout->count; f++)
                                {
                                    u32 sz = layout->fields[f].size;
                                    void *ptr = (u8 *)fields[f] + sz * perSysIdx;
                                    ImGui::PushID((int)(5000 + f));
                                    if      (sz == sizeof(f32))  ImGui::DragFloat(layout->fields[f].name,  (f32*)ptr, 0.01f);
                                    else if (sz == sizeof(Vec3)) ImGui::DragFloat3(layout->fields[f].name, (f32*)ptr, 0.01f);
                                    else if (sz == sizeof(Vec4)) ImGui::DragFloat4(layout->fields[f].name, (f32*)ptr, 0.01f);
                                    else if (sz == sizeof(Vec2)) ImGui::DragFloat2(layout->fields[f].name, (f32*)ptr, 0.01f);
                                    else ImGui::Text("%s (%u bytes)", layout->fields[f].name, sz);
                                    ImGui::PopID();
                                }
                            }
                        }
                        else ImGui::TextDisabled("Entity not in this archetype's runtime set");
                    }
                    else
                    {
                        StructLayout *layout = &archEntry->layout;
                        if (layout->fields)
                        {
                            for (u32 f = 0; f < layout->count; f++)
                            {
                                u32 fsz = layout->fields[f].size;
                                if (fsz > 0) ImGui::TextDisabled("  %s  (%u bytes)", layout->fields[f].name, fsz);
                                else         ImGui::TextDisabled("  %s", layout->fields[f].name);
                            }
                        }
                        else ImGui::TextDisabled("  %u fields (start play to inspect)", layout->count);
                    }
                }
            }
        }

        // ---- Raw SoA Fields (debug) ----
        drawSoAEditorSection(inspectorEntityID);

        break;
    }

    ImGui::End();
}



// Initialize all FBOs and screen quad
void initMultiFBOs()
{
    // Create screen quad mesh if not already created
    if (screenQuadMesh == nullptr)
    {
        screenQuadMesh = createQuadMesh();
    }
    
    // Initialize all FBOs as empty - they will be created on first resize
    for (u32 i = 0; i < MAX_FBOS; i++)
    {
        memset(&viewportFBs[i], 0, sizeof(Framebuffer));
    }
    memset(&finalDisplayFB, 0, sizeof(Framebuffer));
    
    // Reset viewport dimensions
    viewportWidth = 0;
    viewportHeight = 0;
    activeFBO = 0;
}

// Cleanup all FBOs and screen quad
void destroyMultiFBOs()
{
    for (u32 i = 0; i < MAX_FBOS; i++)
    {
        if (viewportFBs[i].fbo != 0)
        {
            destroyFramebuffer(&viewportFBs[i]);
            memset(&viewportFBs[i], 0, sizeof(Framebuffer));
        }
    }
    
    if (finalDisplayFB.fbo != 0) 
    {
        destroyFramebuffer(&finalDisplayFB);
        memset(&finalDisplayFB, 0, sizeof(Framebuffer));
    }
    
    if (screenQuadMesh)
    {
        freeMesh(screenQuadMesh);
        screenQuadMesh = nullptr;
    }
}

// Render FBO texture to screen using screen quad
void renderFBOToScreen(u32 fboIndex, u32 shaderProgram)
{
    if (fboIndex >= MAX_FBOS || !screenQuadMesh || viewportFBs[fboIndex].fbo == 0 || shaderProgram == 0) return;
    
    glDisable(GL_DEPTH_TEST);
    glUseProgram(shaderProgram);
    
    // Bind texture to unit 0 (OpenGL will use this by default)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, viewportFBs[fboIndex].texture);
    
    // Draw screen quad using glDrawArrays since it has no indices
    glBindVertexArray(screenQuadMesh->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    
    glEnable(GL_DEPTH_TEST);
}

static void drawSkyboxSettingsWindow()
{
    if (!showSkyboxSettings) return;

    ImGui::Begin("Skybox Settings", &showSkyboxSettings);
    
    static const c8 *faceLabels[6] = {
        "Right  (+X)", "Left   (-X)", "Top    (+Y)",
        "Bottom (-Y)", "Front  (+Z)", "Back   (-Z)"
    };

    // Per-face texture path inputs
    static c8 faceInputs[6][512] = {0};
    static b8 initInputs = false;
    if (!initInputs)
    {
        for (u32 i = 0; i < 6; i++)
            strncpy(faceInputs[i], g_skyboxFaces[i], sizeof(faceInputs[i]) - 1);
        initInputs = true;
    }

    ImGui::Text("Set each cubemap face texture:");
    ImGui::Separator();

    for (u32 i = 0; i < 6; i++)
    {
        ImGui::PushID((int)i);
        ImGui::Text("%s", faceLabels[i]);
        ImGui::SameLine(120.0f);
        ImGui::SetNextItemWidth(-1.0f);
        c8 label[32];
        snprintf(label, sizeof(label), "##face%u", i);
        ImGui::InputText(label, faceInputs[i], sizeof(faceInputs[i]));
        ImGui::PopID();
    }

    ImGui::Separator();

    if (ImGui::Button("Apply"))
    {
        const c8 *ptrs[6];
        for (u32 i = 0; i < 6; i++) ptrs[i] = faceInputs[i];
        reloadSkyboxFromFaces(ptrs);
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply To Project"))
    {
        const c8 *ptrs[6];
        for (u32 i = 0; i < 6; i++) ptrs[i] = faceInputs[i];
        saveSkyboxFacesToProject(ptrs);
    }

    ImGui::SameLine();

    // Quick-fill from folder shortcut
    static c8 folderInput[512] = {0};
    ImGui::SetNextItemWidth(250.0f);
    ImGui::InputText("##folderPath", folderInput, sizeof(folderInput));
    ImGui::SameLine();
    if (ImGui::Button("Load Folder"))
    {
        if (folderInput[0] != '\0')
        {
            const c8 *suffixes[6] = {"right.jpg","left.jpg","top.jpg","bottom.jpg","front.jpg","back.jpg"};
            for (u32 i = 0; i < 6; i++)
                snprintf(faceInputs[i], sizeof(faceInputs[i]), "%s/%s", folderInput, suffixes[i]);
        }
    }

    // Quick-pick: use project's res/Textures/Skybox
    if (hubProjectDir[0] != '\0')
    {
        if (ImGui::Button("Use Project Skybox"))
        {
            c8 projSkybox[512];
            snprintf(projSkybox, sizeof(projSkybox), "%s/res/Textures/Skybox", hubProjectDir);
            const c8 *suffixes[6] = {"right.jpg","left.jpg","top.jpg","bottom.jpg","front.jpg","back.jpg"};
            for (u32 i = 0; i < 6; i++)
                snprintf(faceInputs[i], sizeof(faceInputs[i]), "%s/%s", projSkybox, suffixes[i]);
            const c8 *ptrs[6];
            for (u32 i = 0; i < 6; i++) ptrs[i] = faceInputs[i];
            reloadSkyboxFromFaces(ptrs);
        }
    }

    ImGui::Separator();

    // Show error feedback from last load attempt
    if (g_skyboxError[0] != '\0')
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", g_skyboxError);
    }

    ImGui::Text("Cubemap Texture ID: %u", cubeMapTexture);

    ImGui::End();
}

// ---- Resource Manager tabs ----

static void drawResourceTab_Textures()
{
    if (!resources) return;
    static c8 texFilter[128] = "";
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("Filter##texF", texFilter, sizeof(texFilter));
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u used", resources->textureUsed, resources->textureCount);
    ImGui::Separator();

    const f32 thumbSize = 64.0f;
    ImGui::BeginChild("##texList", ImVec2(0, 0), false);
    for (u32 i = 0; i < resources->textureIDs.capacity; i++)
    {
        if (!resources->textureIDs.pairs[i].occupied) continue;
        const c8 *name = (const c8 *)resources->textureIDs.pairs[i].key;
        u32 idx        = *(u32 *)resources->textureIDs.pairs[i].value;
        if (idx >= resources->textureUsed) continue;
        if (texFilter[0] != '\0' && !strstr(name, texFilter)) continue;

        u32 glHandle = resources->textureHandles[idx];
        ImGui::PushID((int)idx);
        if (glHandle)
            ImGui::Image((void *)(intptr_t)glHandle, ImVec2(thumbSize, thumbSize), ImVec2(0,1), ImVec2(1,0));
        else
        {
            ImGui::Dummy(ImVec2(thumbSize, thumbSize));
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                IM_COL32(120,120,120,180));
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", name);
        ImGui::TextDisabled("slot %u  GL %u", idx, glHandle);
        ImGui::EndGroup();
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

static void drawResourceTab_Shaders()
{
    if (!resources) return;
    ImGui::TextDisabled("%u project  +  %u editor  shaders tracked",
        g_trackedProjectShaderCount, g_editorShaderCount);
    ImGui::Separator();

    ImGui::BeginChild("##shaderList", ImVec2(0, 0), false);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.75f, 0.3f, 1.0f));
    ImGui::TextUnformatted("Editor shaders");
    ImGui::PopStyleColor();
    ImGui::Separator();
    for (u32 i = 0; i < g_editorShaderCount; i++)
    {
        EditorTrackedShader &e = g_editorShaders[i];
        ImGui::PushID((int)i);
        ImGui::BeginGroup();
        ImGui::Text("%s", e.vertPath);
        ImGui::TextDisabled("frag: %s  GL: %u", e.fragPath, *e.handlePtr);
        if (ImGui::SmallButton("Recompile"))
        {
            u32 old = *e.handlePtr;
            u32 np  = createGraphicsProgram(e.vertPath, e.fragPath);
            if (np) {
                if (shaderHandles && old)
                    for (u32 j = 0; j < entityCount; j++)
                        if (shaderHandles[j] == old) shaderHandles[j] = np;
                freeShader(old);
                *e.handlePtr = np;
                e.vertMtime = getFileMtime(e.vertPath);
                e.fragMtime = getFileMtime(e.fragPath);
                if (e.handlePtr == &shader)       materialUniforms = getMaterialUniforms(shader);
                else if (e.handlePtr == &arrowShader) colourLocation = glGetUniformLocation(arrowShader, "colour");
                else if (e.handlePtr == &skyboxShader) {
                    skyboxViewLoc = glGetUniformLocation(skyboxShader, "view");
                    skyboxProjLoc = glGetUniformLocation(skyboxShader, "projection");
                } else if (e.handlePtr == &idShader) idLocation = glGetUniformLocation(idShader, "entityID");
                INFO("Recompiled: %s", e.vertPath);
            } else ERROR("Failed to recompile: %s", e.vertPath);
        }
        ImGui::EndGroup();
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 0.5f, 1.0f));
    ImGui::TextUnformatted("Project shaders");
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (g_trackedProjectShaderCount == 0)
        ImGui::TextDisabled("No project shaders loaded.");
    for (u32 i = 0; i < g_trackedProjectShaderCount; i++)
    {
        TrackedProjectShader &t = g_trackedProjectShaders[i];
        if (!t.valid) continue;
        u32 idx = 0;
        u32 glHandle = 0;
        if (findInMap(&resources->shaderIDs, t.name, &idx))
            glHandle = resources->shaderHandles[idx];
        ImGui::PushID((int)(1000 + i));
        ImGui::BeginGroup();
        ImGui::Text("%s", t.name);
        ImGui::TextDisabled("vert: %s  GL: %u", t.vertPath, glHandle);
        ImGui::TextDisabled("frag: %s", t.fragPath);
        if (ImGui::SmallButton("Recompile##proj"))
        {
            u32 np = createGraphicsProgram(t.vertPath, t.fragPath);
            if (np && glHandle) {
                if (shaderHandles)
                    for (u32 j = 0; j < entityCount; j++)
                        if (shaderHandles[j] == glHandle) shaderHandles[j] = np;
                freeShader(glHandle);
                resources->shaderHandles[idx] = np;
                t.vertMtime = getFileMtime(t.vertPath);
                t.fragMtime = getFileMtime(t.fragPath);
                INFO("Recompiled project shader: %s", t.name);
            } else ERROR("Failed to recompile project shader: %s", t.name);
        }
        ImGui::EndGroup();
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

static void drawResourceTab_Models()
{
    if (!resources) return;

    // Left panel — model list
    ImGui::BeginChild("##modelList", ImVec2(220, 0), true);
    ImGui::TextDisabled("%u / %u models", resources->modelUsed, resources->modelCount);
    ImGui::Separator();
    for (u32 i = 0; i < resources->modelIDs.capacity; i++)
    {
        if (!resources->modelIDs.pairs[i].occupied) continue;
        const c8 *name = (const c8 *)resources->modelIDs.pairs[i].key;
        u32 idx        = *(u32 *)resources->modelIDs.pairs[i].value;
        if (idx >= resources->modelUsed) continue;
        Model *m = &resources->modelBuffer[idx];
        ImGui::PushID((int)idx);
        bool selected = (g_selectedModelIdx == (i32)idx);
        c8 label[128];
        snprintf(label, sizeof(label), "%s##mdl%u", name, idx);
        if (ImGui::Selectable(label, selected))
            g_selectedModelIdx = (i32)idx;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("meshes: %u  radius: %.2f", m->meshCount, m->boundingRadius);
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel — preview + info
    ImGui::BeginChild("##modelDetail", ImVec2(0, 0), false);

    if (g_selectedModelIdx < 0 || (u32)g_selectedModelIdx >= resources->modelUsed)
    {
        ImGui::TextDisabled("Select a model from the list.");
        ImGui::EndChild();
        return;
    }

    u32 modelIdx = (u32)g_selectedModelIdx;
    Model *m = &resources->modelBuffer[modelIdx];

    // Find model name
    const c8 *modelName = "(unnamed)";
    for (u32 i = 0; i < resources->modelIDs.capacity; i++)
    {
        if (!resources->modelIDs.pairs[i].occupied) continue;
        if (*(u32 *)resources->modelIDs.pairs[i].value == modelIdx)
        { modelName = (const c8 *)resources->modelIDs.pairs[i].key; break; }
    }

    // Rendered preview centred in panel
    renderModelPreview(modelIdx);
    if (g_modelPreviewOutputFBO.texture)
    {
        static const f32 MDL_PREV = 180.0f;
        f32 panelW  = ImGui::GetContentRegionAvail().x;
        f32 offsetX = (panelW - MDL_PREV) * 0.5f;
        if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        ImGui::Image(
            (void *)(intptr_t)g_modelPreviewOutputFBO.texture,
            ImVec2(MDL_PREV, MDL_PREV),
            ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deferred preview with scene lights");
    }

    ImGui::Spacing();
    ImGui::Text("%s", modelName);
    ImGui::Separator();
    ImGui::TextDisabled("Slot:          %u", modelIdx);
    ImGui::TextDisabled("Meshes:        %u", m->meshCount);
    ImGui::TextDisabled("Materials:     %u", m->materialCount);
    ImGui::TextDisabled("Bounding r:    %.3f", m->boundingRadius);

    // Per-mesh material assignments
    if (m->meshCount > 0 && m->materialIndices)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("Mesh materials:");
        for (u32 mi = 0; mi < m->meshCount && mi < m->materialCount; mi++)
        {
            u32 matIdx = m->materialIndices[mi];
            const c8 *matName = "(unknown)";
            if (matIdx < resources->materialUsed)
            {
                for (u32 k = 0; k < resources->materialIDs.capacity; k++)
                {
                    if (!resources->materialIDs.pairs[k].occupied) continue;
                    if (*(u32 *)resources->materialIDs.pairs[k].value == matIdx)
                    { matName = (const c8 *)resources->materialIDs.pairs[k].key; break; }
                }
            }
            ImGui::TextDisabled("  [%u] %s", mi, matName);
        }
    }

    ImGui::EndChild();
}

static void drawResourceTab_Materials()
{
    if (!resources) return;

    // Left panel — material list
    ImGui::BeginChild("##matList", ImVec2(220, 0), true);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##filter", g_matNameFilter, sizeof(g_matNameFilter));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter materials by name");
    ImGui::Separator();

    for (u32 i = 0; i < resources->materialIDs.capacity; i++)
    {
        if (!resources->materialIDs.pairs[i].occupied) continue;
        const c8 *name = (const c8 *)resources->materialIDs.pairs[i].key;
        u32 idx        = *(u32 *)resources->materialIDs.pairs[i].value;
        if (idx >= resources->materialUsed) continue;
        if (g_matNameFilter[0] != '\0' && !strstr(name, g_matNameFilter)) continue;
        bool selected = (g_selectedMaterialIdx == (i32)idx);
        ImGui::PushID((int)idx);
        if (ImGui::Selectable(name, selected))
            g_selectedMaterialIdx = (i32)idx;
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("+ New Material", ImVec2(-1, 0)))
    {
        if (resources->materialUsed < resources->materialCount)
        {
            u32 newIdx = resources->materialUsed;
            Material blank = {0};
            blank.colour    = {1.0f, 1.0f, 1.0f};
            blank.roughness = 0.5f;
            resources->materialBuffer[newIdx] = blank;
            c8 newName[64];
            snprintf(newName, sizeof(newName), "Material_%u", newIdx);
            insertMap(&resources->materialIDs, newName, &resources->materialUsed);
            resources->materialUsed++;
            g_selectedMaterialIdx = (i32)newIdx;
            INFO("Created material '%s' at index %u", newName, newIdx);
        }
        else WARN("Material buffer full");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel — preview + editor
    ImGui::BeginChild("##matEdit", ImVec2(0, 0), false);
    if (g_selectedMaterialIdx < 0 || (u32)g_selectedMaterialIdx >= resources->materialUsed)
    {
        ImGui::TextDisabled("Select a material from the list to edit it.");
        ImGui::EndChild();
        return;
    }

    u32 matIdx = (u32)g_selectedMaterialIdx;
    Material *mat = &resources->materialBuffer[matIdx];

    const c8 *matName = "(unnamed)";
    for (u32 i = 0; i < resources->materialIDs.capacity; i++)
    {
        if (!resources->materialIDs.pairs[i].occupied) continue;
        if (*(u32 *)resources->materialIDs.pairs[i].value == matIdx)
        { matName = (const c8 *)resources->materialIDs.pairs[i].key; break; }
    }

    // Preview centred above properties (stacked vertically to keep the panel narrow)
    static const f32 PREV_DISP = 180.0f;
    if (deferredLightingShader && screenQuadMesh)
    {
        renderMaterialPreview(matIdx);
        f32 panelW  = ImGui::GetContentRegionAvail().x;
        f32 offsetX = (panelW - PREV_DISP) * 0.5f;
        if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        ImGui::Image(
            (void *)(intptr_t)g_matPreviewOutputFBO.texture,
            ImVec2(PREV_DISP, PREV_DISP),
            ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Full deferred pipeline preview with scene lights");
    }
    else ImGui::TextDisabled("Preview unavailable (deferred shader not loaded)");

    ImGui::Spacing();
    ImGui::Text("Material: %s  [%u]", matName, matIdx);
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1); drawTextureSelector("Albedo",    &mat->albedoTex,    "##RegAlb");
    ImGui::SetNextItemWidth(-1); drawTextureSelector("Normal",    &mat->normalTex,    "##RegNrm");
    ImGui::SetNextItemWidth(-1); drawTextureSelector("Metallic",  &mat->metallicTex,  "##RegMet");
    ImGui::SetNextItemWidth(-1); drawTextureSelector("Roughness", &mat->roughnessTex, "##RegRou");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1); ImGui::ColorEdit3("Colour##reg",        (f32 *)&mat->colour);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Metallic##reg",     &mat->metallic,       0.0f, 1.0f);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Roughness##reg",    &mat->roughness,      0.0f, 1.0f);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Emissive##reg",     &mat->emissive,       0.0f, 10.0f);
    ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("Transparency##reg", &mat->transparency,   0.0f, 1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    if (currentInspectorState == ENTITY_VIEW && inspectorEntityID < entityCount && entityMaterialIDs)
    {
        if (ImGui::Button("Assign to Selected Entity"))
        {
            entityMaterialIDs[inspectorEntityID] = matIdx;
            INFO("Assigned mat %u to entity %u", matIdx, inspectorEntityID);
        }
        ImGui::SameLine();
        const c8 *ename = names ? &names[inspectorEntityID * MAX_NAME_SIZE] : "?";
        ImGui::TextDisabled("-> %s", (ename && ename[0]) ? ename : "(unnamed)");
    }
    else ImGui::TextDisabled("Select an entity in the scene to assign this material.");

    ImGui::EndChild();
}

static void drawResourceManagerWindow()
{
    if (!showResourceManager) return;
    if (!resources) return;

    ImGui::SetNextWindowSize(ImVec2(660, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Resource Manager", &showResourceManager))
    { ImGui::End(); return; }

    // If tab was set externally (e.g. from inspector Edit button), jump to it
    static i32 pendingTab = -1;
    if (g_resourceManagerTab >= 0) { pendingTab = g_resourceManagerTab; g_resourceManagerTab = -1; }

    if (ImGui::BeginTabBar("##resTabs"))
    {
        auto setTabActive = [&](i32 tabIdx) -> ImGuiTabItemFlags {
            if (pendingTab == tabIdx) { pendingTab = -1; return ImGuiTabItemFlags_SetSelected; }
            return ImGuiTabItemFlags_None;
        };

        if (ImGui::BeginTabItem("Textures", nullptr, setTabActive(0)))
        { drawResourceTab_Textures(); ImGui::EndTabItem(); }

        if (ImGui::BeginTabItem("Shaders", nullptr, setTabActive(1)))
        { drawResourceTab_Shaders(); ImGui::EndTabItem(); }

        if (ImGui::BeginTabItem("Models", nullptr, setTabActive(2)))
        { drawResourceTab_Models(); ImGui::EndTabItem(); }

        if (ImGui::BeginTabItem("Materials", nullptr, setTabActive(3)))
        { drawResourceTab_Materials(); ImGui::EndTabItem(); }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

static b8 showBuildLog = false;

//=====================================================================================================================
// loaded entities
//=====================================================================================================================
static void drawLoadedEntitiesWindow()
{
    if (!showLoadedEntities) return;

    ImGui::Begin("Loaded Entities", &showLoadedEntities);

    u32 totalEntities = 0;
    u32 totalArchetypes = 0;

    for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        Archetype *arch = &g_ecsArchetypes[a];
        if (!arch->layout || !arch->arena) continue;

        u32 count = 0;
        for (u32 c = 0; c < arch->activeChunkCount; c++)
            count += arch->arena[c].count;
        if (count == 0) continue;

        totalArchetypes++;
        totalEntities += count;

        // Collapsible header per archetype
        c8 header[128];
        snprintf(header, sizeof(header), "%s (%u entities)###arch%u",
                 g_archRegistry.entries[a].name, count, a);

        if (ImGui::CollapsingHeader(header))
        {
            StructLayout *layout = arch->layout;

            // Find common field indices for display
            i32 posXIdx = -1, posYIdx = -1, posZIdx = -1;
            i32 scaleXIdx = -1, scaleYIdx = -1, scaleZIdx = -1;
            i32 aliveIdx = -1, modelIdx = -1;
            for (u32 f = 0; f < layout->count; f++)
            {
                const c8 *n = layout->fields[f].name;
                if      (strcmp(n, "PositionX") == 0) posXIdx   = (i32)f;
                else if (strcmp(n, "PositionY") == 0) posYIdx   = (i32)f;
                else if (strcmp(n, "PositionZ") == 0) posZIdx   = (i32)f;
                else if (strcmp(n, "ScaleX")    == 0) scaleXIdx = (i32)f;
                else if (strcmp(n, "ScaleY")    == 0) scaleYIdx = (i32)f;
                else if (strcmp(n, "ScaleZ")    == 0) scaleZIdx = (i32)f;
                else if (strcmp(n, "Alive")     == 0) aliveIdx  = (i32)f;
                else if (strcmp(n, "ModelID")   == 0) modelIdx  = (i32)f;
            }

            // Show field layout
            if (ImGui::TreeNode("Fields"))
            {
                for (u32 f = 0; f < layout->count; f++)
                    ImGui::BulletText("[%u] %s (%u bytes)", f, layout->fields[f].name, layout->fields[f].size);
                ImGui::TreePop();
            }

            // Entity list — only iterate chunk 0 for simplicity (single-chunk archetypes)
            for (u32 c = 0; c < arch->activeChunkCount; c++)
            {
                void **fields = getArchetypeFields(arch, c);
                if (!fields) continue;
                u32 chunkCount = arch->arena[c].count;

                f32 *posX  = posXIdx   >= 0 ? (f32 *)fields[posXIdx]   : nullptr;
                f32 *posY  = posYIdx   >= 0 ? (f32 *)fields[posYIdx]   : nullptr;
                f32 *posZ  = posZIdx   >= 0 ? (f32 *)fields[posZIdx]   : nullptr;
                f32 *scX   = scaleXIdx >= 0 ? (f32 *)fields[scaleXIdx] : nullptr;
                f32 *scY   = scaleYIdx >= 0 ? (f32 *)fields[scaleYIdx] : nullptr;
                f32 *scZ   = scaleZIdx >= 0 ? (f32 *)fields[scaleZIdx] : nullptr;
                b8  *alive = aliveIdx  >= 0 ? (b8  *)fields[aliveIdx]  : nullptr;
                u32 *model = modelIdx  >= 0 ? (u32 *)fields[modelIdx]  : nullptr;

                // Clamp displayed count to avoid UI freeze with millions of entities
                u32 displayMax = chunkCount < 500 ? chunkCount : 500;

                if (chunkCount > displayMax)
                    ImGui::TextDisabled("Showing %u / %u (capped for performance)", displayMax, chunkCount);

                if (ImGui::BeginTable("##entities", 5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                    ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 16)))
                {
                    ImGui::TableSetupColumn("ID",    ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupColumn("Alive", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                    ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Scale",    ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin((int)displayMax);
                    while (clipper.Step())
                    {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                        {
                            u32 e = (u32)row;
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%u", e);

                            ImGui::TableSetColumnIndex(1);
                            if (alive)
                                ImGui::TextColored(alive[e] ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
                                                   alive[e] ? "Y" : "N");
                            else
                                ImGui::TextDisabled("-");

                            ImGui::TableSetColumnIndex(2);
                            if (model)
                                ImGui::Text("%u", model[e]);
                            else
                                ImGui::TextDisabled("-");

                            ImGui::TableSetColumnIndex(3);
                            if (posX && posY && posZ)
                                ImGui::Text("%.1f, %.1f, %.1f", posX[e], posY[e], posZ[e]);
                            else
                                ImGui::TextDisabled("-");

                            ImGui::TableSetColumnIndex(4);
                            if (scX && scY && scZ)
                                ImGui::Text("%.2f, %.2f, %.2f", scX[e], scY[e], scZ[e]);
                            else
                                ImGui::TextDisabled("-");
                        }
                    }
                    ImGui::EndTable();
                }
            }
        }
    }

    if (totalArchetypes == 0)
        ImGui::TextDisabled("No runtime archetypes loaded. Run the game (F5) to see entities.");
    else
        ImGui::Text("Total: %u archetypes, %u entities", totalArchetypes, totalEntities);

    ImGui::End();
}

static void drawBuildLogWindow()
{
    if (!showBuildLog) return;

    ImGui::Begin("Build Log", &showBuildLog);
    if (g_buildInProgress)
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Building...");
    else
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Idle");

    ImGui::Separator();

    // Read-only multiline text — supports selection and Ctrl+C
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##buildlog", g_buildLog, sizeof(g_buildLog),
                              avail, ImGuiInputTextFlags_ReadOnly);
    ImGui::End();
}

// temporary file used to snapshot the scene before play-mode
static const c8 *PLAY_SNAPSHOT_PATH = "__play_snapshot__.drsc";
static b8 g_hasPlaySnapshot = false;

static void snapshotScene()
{
    SceneData sd = {0};
    sd.archetypeCount = 1;
    sd.archetypes = &sceneArchetype;
    strncpy(sd.archetypeNames[0], "SceneEntity", MAX_SCENE_NAME - 1);
    sceneArchetype.arena[0].count = entityCount;
    sceneArchetype.cachedEntityCount = entityCount;
    if (entityCount > 0 && sceneArchetype.activeChunkCount == 0)
        sceneArchetype.activeChunkCount = 1;
    sd.materialCount = resources->materialUsed;
    sd.materials     = resources->materialBuffer;

    c8 fullPath[MAX_PATH_LENGTH];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", hubProjectDir, PLAY_SNAPSHOT_PATH);
    g_hasPlaySnapshot = saveScene(fullPath, &sd);
    if (!g_hasPlaySnapshot)
        WARN("Failed to snapshot scene before play");
}

static void restoreSnapshot()
{
    if (!g_hasPlaySnapshot) return;

    c8 fullPath[MAX_PATH_LENGTH];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", hubProjectDir, PLAY_SNAPSHOT_PATH);

    SceneData sd = loadScene(fullPath);
    if (sd.archetypeCount > 0 && sd.archetypes)
    {
        sceneRemapModelIDs(&sd);

        // During play mode the game DLL may have reallocated archetype arenas,
        // leaving stale pointers. Zero out before destroy to avoid heap corruption.
        // intentional leak — one-time play-mode allocs
        if (sceneArchetype.arena)
        {
            for (u32 ci = 0; ci < sceneArchetype.arenaCount; ci++)
            {
                sceneArchetype.arena[ci].fields = nullptr;
                sceneArchetype.arena[ci].data   = nullptr;
            }
        }
        destroyArchetype(&sceneArchetype);
        sceneArchetype  = sd.archetypes[0];
        dfree(sd.archetypes, sizeof(Archetype) * sd.archetypeCount, MEM_TAG_SCENE);
        sd.archetypes = nullptr;
        entitySizeCache = sceneArchetype.capacity;
        entityCount     = sceneArchetype.arena[0].count;
        entitySize      = (i32)entitySizeCache;
        migrateSceneArchetypeIfNeeded();
        rebindArchetypeFields();
        applySceneCameraEntityToSceneCam();

        if (sd.materialCount > 0 && sd.materials)
        {
            u32 count = sd.materialCount;
            if (count > resources->materialCount)
                count = resources->materialCount;
            memcpy(resources->materialBuffer, sd.materials,
                   sizeof(Material) * count);
            resources->materialUsed = count;
            dfree(sd.materials, sizeof(Material) * sd.materialCount, MEM_TAG_SCENE);
        }
        if (sd.modelRefs)
            dfree(sd.modelRefs, sizeof(c8[MAX_NAME_SIZE]) * sd.modelRefCount, MEM_TAG_SCENE);

        inspectorEntityID     = 0;
        currentInspectorState = EMPTY_VIEW;
        manipulateTransform   = false;
        INFO("Scene restored from play-mode snapshot");
    }
    else
    {
        WARN("Failed to restore scene from snapshot");
    }

    remove(fullPath);
    g_hasPlaySnapshot = false;
}

void doBuildAndRun()
{
    if (g_gameRunning && g_gameDLL.loaded)
    {
        g_gameDLL.plugin.destroy();
        unloadGameDLL(&g_gameDLL);
        g_gameRunning = false;
    }

    showBuildLog = true;

    if (!buildProject(hubProjectDir, g_buildLog, sizeof(g_buildLog)))
    {
        ERROR("Build failed");
        return;
    }

    c8 dllPath[MAX_PATH_LENGTH];
    b8 dllLoaded = false;

#ifdef _WIN32
    // Try common DLL naming conventions and output directories (MinGW, MSVC Debug/Release)
    static const c8 *dllCandidates[] = {
        "bin/libgame.dll",
        "bin/game.dll",
        "bin/Debug/game.dll",
        "bin/Debug/libgame.dll",
        "bin/Release/game.dll",
        "bin/Release/libgame.dll"
    };
    for (u32 ci = 0; ci < sizeof(dllCandidates) / sizeof(dllCandidates[0]); ci++)
    {
        snprintf(dllPath, sizeof(dllPath), "%s/%s", hubProjectDir, dllCandidates[ci]);
        if (fileExists(dllPath) && loadGameDLL(dllPath, &g_gameDLL))
        {
            dllLoaded = true;
            break;
        }
    }
#else
    snprintf(dllPath, sizeof(dllPath), "%s/bin/libgame.so", hubProjectDir);
    dllLoaded = loadGameDLL(dllPath, &g_gameDLL);
#endif

    if (!dllLoaded)
    {
        ERROR("Failed to load game DLL (tried multiple paths under %s/bin/)", hubProjectDir);
        return;
    }

    // snapshot the current scene so we can restore it when play stops
    snapshotScene();

    applySceneCameraEntityToSceneCam();

    g_gameDLL.plugin.init(hubProjectDir);

    // Auto-discover ECS system entry points from the loaded game DLL.
    // For each registered archetype, try to find druidGetECSSystem_<name>.
    g_ecsSystemCount = 0;
    if (g_archRegistry.count == 0)
        WARN("No archetypes registered — ECS system discovery will be skipped. "
             "Create archetypes in the Prefabs window first.");
    for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        ECSSystemPlugin sys = {0};
        INFO("Looking for ECS entry: druidGetECSSystem_%s", g_archRegistry.entries[a].name);
        if (loadECSSystemFromHandle(&g_gameDLL.dll, g_archRegistry.entries[a].name, &sys))
        {
            g_ecsSystems[a] = sys;
            g_ecsSystemLoaded[a] = true;
            g_ecsSystemCount++;
            if (sys.init) sys.init();
            INFO("Loaded ECS system: %s", g_archRegistry.entries[a].name);
        }
        else
        {
            g_ecsSystemLoaded[a] = false;
        }
    }

    // Create per-system archetypes so each ECS system only touches its own
    // entities (those with a matching archetypeID in the scene).
    populateEcsArchetypes();

    // Initialize physics world and auto-register any physics-body archetypes
    memset(g_archPhysRegistered, 0, sizeof(g_archPhysRegistered));
    physInit(Vec3{0.0f, -9.81f, 0.0f}, 1.0f / 60.0f);
    for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        if (FLAG_CHECK(g_archRegistry.entries[a].flags, ARCH_PHYSICS_BODY) && g_ecsArchetypes[a].arena)
        {
            physRegisterArchetype(physicsWorld, &g_ecsArchetypes[a]);
            g_archPhysRegistered[a] = true;
        }
    }

    // Register static scene-entity colliders (floor, walls, etc.) with physics
    setupScenePhysicsArchetype();
    if (g_scenePhysCreated)
    {
        physRegisterArchetype(physicsWorld, &g_scenePhysArch);
        INFO("Registered scene physics archetype with physics world (count=%u)",
             g_scenePhysArch.arena[0].count);
    }
    else
    {
        WARN("Scene physics archetype was NOT created — no floor collisions");
    }

    g_gameRunning = true;
    INFO("Game started (%u ECS systems discovered)", g_ecsSystemCount);
}

void doStopGame()
{
    if (!g_gameRunning) return;

    // Destroy ECS systems before unloading the DLL
    for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
    {
        if (g_ecsSystemLoaded[a] && g_ecsSystems[a].destroy)
            g_ecsSystems[a].destroy();
        g_ecsSystemLoaded[a] = false;
        memset(&g_ecsSystems[a], 0, sizeof(ECSSystemPlugin));
    }
    g_ecsSystemCount = 0;

    // Shutdown physics world before archetypes are destroyed
    physShutdown();
    memset(g_archPhysRegistered, 0, sizeof(g_archPhysRegistered));
    memset(g_colliderCache, 0, sizeof(g_colliderCache));

    // Destroy scene physics archetype (uses static layout, safe to destroy anytime)
    cleanupScenePhysicsArchetype();

    // Destroy per-system archetypes (must happen while DLL is still loaded
    // since the layouts belong to the DLL).
    cleanupEcsArchetypes();

    if (g_gameDLL.loaded)
        g_gameDLL.plugin.destroy();

    unloadGameDLL(&g_gameDLL);
    g_gameRunning = false;

    // restore the scene to its pre-play state
    restoreSnapshot();
    INFO("Game stopped");
}

static void drawCameraSettingsWindow()
{
    if (!showCameraSettings) return;

    ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);
    ImGui::Begin("Camera Settings", &showCameraSettings);

    ImGui::SliderFloat("FOV",      &g_editorFov,  10.0f,  170.0f, "%.1f deg");
    ImGui::SliderFloat("Near Clip",&g_editorNear,  0.001f, 10.0f,  "%.3f",  ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Far Clip", &g_editorFar,   10.0f,  100000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);

    ImGui::Spacing();
    ImGui::TextDisabled("Current: FOV %.1f  Near %.4f  Far %.0f", g_editorFov, g_editorNear, g_editorFar);

    if (ImGui::Button("Reset to defaults"))
    {
        g_editorFov  = 70.0f;
        g_editorNear = 0.1f;
        g_editorFar  = 1000.0f;
    }

    ImGui::End();
}

void drawDockspaceAndPanels()
{
    static b8 dockspaceOpen = true;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |=
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.25f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", &dockspaceOpen, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceID");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    // Main menu bar
    if (ImGui::BeginMenuBar())
    {

        // Scene UI
        if (ImGui::BeginMenu("Scene"))
        {
            if (ImGui::MenuItem("New Scene"))
            {
                // Optionally show a modal to set name/capacity
                showNewSceneModal = true;
            }
            if (ImGui::MenuItem("Save Scene As..."))
            {
                showSaveModal = true;
            }
            if (ImGui::MenuItem("Load Scene..."))
            {
                showLoadModal = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Skybox Settings..."))
            {
                showSkyboxSettings = true;
            }
            ImGui::Separator();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Profiler", NULL, showProfiler))
            {
                showProfiler = !showProfiler;
            }
            if (ImGui::MenuItem("Build Log", NULL, showBuildLog))
            {
                showBuildLog = !showBuildLog;
            }
            if (ImGui::MenuItem("Scenes", NULL, showScenesPanel))
            {
                showScenesPanel = !showScenesPanel;
            }
            if (ImGui::MenuItem("Camera Settings", NULL, showCameraSettings))
            {
                showCameraSettings = !showCameraSettings;
            }
            if (ImGui::MenuItem("Draw Colliders", NULL, showColliders))
            {
                showColliders = !showColliders;
            }
            if (ImGui::MenuItem("Draw Lights", NULL, showLightGizmos))
            {
                showLightGizmos = !showLightGizmos;
            }
            if (ImGui::MenuItem("Loaded Entities", NULL, showLoadedEntities))
                showLoadedEntities = !showLoadedEntities;
            if (ImGui::MenuItem("Resource Manager", NULL, showResourceManager))
                showResourceManager = !showResourceManager;
            ImGui::EndMenu();
        }

        // project menu
        if (ImGui::BeginMenu("Project"))
        {
            if (ImGui::MenuItem("Build", "Ctrl+B"))
            {
                showBuildLog = true;
                buildProject(hubProjectDir, g_buildLog, sizeof(g_buildLog));
            }
            if (ImGui::MenuItem("Build & Run", "F5"))
            {
                doBuildAndRun();
            }
            if (ImGui::MenuItem("Stop", "Shift+F5", false, g_gameRunning))
            {
                doStopGame();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Regenerate Default Files"))
            {
                if (generateProjectFiles(hubProjectDir))
                    INFO("Regenerated default project files.");
                else
                    ERROR("Failed to regenerate project files.");
            }
            if (ImGui::MenuItem("Update Engine Files"))
            {
                showBuildLog = true;
                if (updateProject(hubProjectDir, g_buildLog, sizeof(g_buildLog)))
                    INFO("Engine files updated in project.");
                else
                    ERROR("Failed to update engine files. Check Build Log for details.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build Game (Standalone)"))
            {
                showBuildLog = true;
                if (buildStandalone(hubProjectDir, scenePathBuffer, g_buildLog, sizeof(g_buildLog)))
                    INFO("Standalone game built and packaged to export/");
                else
                    ERROR("Failed to build standalone game.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reload Resources"))
            {
                reloadProjectResources();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Project Folder"))
            {
                if (hubProjectDir[0] != '\0')
                {
#ifdef _WIN32
                    c8 normalized[MAX_PATH_LENGTH];
                    strncpy(normalized, hubProjectDir, sizeof(normalized) - 1);
                    normalized[sizeof(normalized) - 1] = '\0';
                    for (u32 i = 0; normalized[i] != '\0'; i++)
                        if (normalized[i] == '\\') normalized[i] = '/';

                    c8 url[MAX_PATH_LENGTH + 16];
                    snprintf(url, sizeof(url), "file:///%s", normalized);
                    if (!SDL_OpenURL(url))
                        ERROR("Failed to open project folder: %s", hubProjectDir);
#elif __APPLE__
                    c8 cmd[1024];
                    snprintf(cmd, sizeof(cmd), "open \"%s\"", hubProjectDir);
                    system(cmd);
#else
                    c8 cmd[1024];
                    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", hubProjectDir);
                    system(cmd);
#endif
                }
            }
            ImGui::EndMenu();
        }

        // run/stop buttons
        ImGui::Separator();
        if (!g_gameRunning)
        {
            if (ImGui::MenuItem("  > Run  "))
                doBuildAndRun();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::MenuItem("  [] Stop  "))
                doStopGame();
            ImGui::PopStyleColor();
        }

        ImGui::EndMenuBar();
    }

    // Build default layout only on first run or if no saved layout loaded
    static b8 first_time = true;
    if (first_time)
    {
        first_time = false;
        ImGuiDockNode *existingNode = ImGui::DockBuilderGetNode(dockspace_id);
        if (!existingNode || !existingNode->IsSplitNode())
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(
                dockspace_id,
                dockspace_flags | ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

            ImGuiID dock_id_middle = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);
            ImGuiID dock_id_prefabs = ImGui::DockBuilderSplitNode(
                dock_id_middle, ImGuiDir_Up, 0.5f, nullptr, &dock_id_middle);

            ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

            ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Inspector", dock_id_right);
            ImGui::DockBuilderDockWindow("Scene List", dock_id_prefabs);
            ImGui::DockBuilderDockWindow("Prefabs", dock_id_middle);
            ImGui::DockBuilderDockWindow("Scenes", dock_id_middle);
            ImGui::DockBuilderDockWindow("Profiler", dock_id_bottom);
            ImGui::DockBuilderDockWindow("Build Log", dock_id_bottom);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    //--actual docked windows--
    drawViewportWindow();
    drawDebugWindow();
    drawPrefabsWindow();
    drawScenesPanel();
    drawSceneListWindow();
    drawInspectorWindow();
    drawProfilerWindow();
    drawSkyboxSettingsWindow();
    drawCameraSettingsWindow();
    drawBuildLogWindow();
    drawConsoleWindow();
    drawLoadedEntitiesWindow();
    drawResourceManagerWindow();
    checkForResourceChanges();

    // Calculate timestep for physics and game updates
    f32 dt = (f32)(1.0 / (editor->fps > 0.0 ? editor->fps : 60.0));

    // Keep physics archetype registration in sync with editor flags.
    // Uses cached flag to avoid O(A*R) search every frame.
    if (physicsWorld)
    {
        for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
        {
            if (g_archPhysRegistered[a]) continue;
            if (!FLAG_CHECK(g_archRegistry.entries[a].flags, ARCH_PHYSICS_BODY))
                continue;

            Archetype *arch = &g_ecsArchetypes[a];
            if (!arch->layout) continue;

            physRegisterArchetype(physicsWorld, arch);
            g_archPhysRegistered[a] = true;
        }
    }

    // Step the physics world in both edit and game mode (for real-time preview)
    physTick(dt);

    // tick game plugin + ECS systems
    if (g_gameRunning && g_gameDLL.loaded)
    {
        g_gameDLL.plugin.update(dt);

        // Run ECS system update callbacks on per-system archetypes
        {
        PROFILE_SCOPE("ECS Update");
        for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
        {
            if (!g_ecsSystemLoaded[a] || !g_ecsSystems[a].update) continue;
            // Runtime-spawn archetypes must tick even with zero scene entities.
            // Prefer explicit buffered flag, but fall back to non-zero poolCapacity
            // in case source metadata parsing missed ARCH_BUFFERED.
            if (g_ecsArchEntityCount[a] > 0
                || FLAG_CHECK(g_ecsArchetypes[a].flags, ARCH_BUFFERED)
                || g_ecsArchetypes[a].poolCapacity > 0)
                g_ecsSystems[a].update(&g_ecsArchetypes[a], dt);
        }
        } // PROFILE_SCOPE("ECS Update")

        // Sync ECS-updated data (position, rotation, …) back to the scene
        // archetype so the editor renderer draws entities correctly.
        syncEcsToScene();

        // Keep placed Player guide model(s) in sync with gameplay camera.
        // This is useful when the gameplay controller runs outside the editor ECS
        // path and still needs a visible third-person proxy in the scene.
        {
            i32 playerArchIdx = -1;
            for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
            {
                if (strcmp(g_archRegistry.entries[a].name, "Player") == 0)
                {
                    playerArchIdx = (i32)a;
                    break;
                }
            }

            if (playerArchIdx >= 0 && archetypeIDs && positions && rotations && renderer)
            {
                Camera *cam = rendererGetCamera(renderer, renderer->activeCamera);
                if (cam)
                {
                    // Keep in sync with Player.c eye offset.
                    const f32 eyeHeight = 1.6f;
                    Vec3 bodyPos = {cam->pos.x, cam->pos.y - eyeHeight, cam->pos.z};
                    for (u32 id = 0; id < entityCount; id++)
                    {
                        if (!isActive[id]) continue;
                        if ((i32)archetypeIDs[id] != playerArchIdx) continue;
                        positions[id] = bodyPos;
                    }
                }
            }
        }
    }

    ImGui::End(); // MainDockSpace

    // --- Scene menu modals ---
    if (showSaveModal)
    {
        ImGui::OpenPopup("Save Scene As");
        showSaveModal = false;
        // Pre-fill with a default name if empty
        if (scenePathBuffer[0] == '\0')
            strncpy(scenePathBuffer, "scene", sizeof(scenePathBuffer) - 1);
    }
    if (ImGui::BeginPopupModal("Save Scene As", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Saves to: %s/scenes/", hubProjectDir);
        ImGui::InputText("Filename", scenePathBuffer, sizeof(scenePathBuffer));
        if (ImGui::Button("Save"))
        {
            // Build full path: <project>/scenes/<name>.drsc
            c8 fullPath[1024];
            // Strip .drsc if user already typed it
            c8 cleanName[512];
            strncpy(cleanName, scenePathBuffer, sizeof(cleanName) - 1);
            cleanName[sizeof(cleanName) - 1] = '\0';
            u32 nameLen = (u32)strlen(cleanName);
            if (nameLen > 5 && strcmp(cleanName + nameLen - 5, ".drsc") == 0)
                cleanName[nameLen - 5] = '\0';
            snprintf(fullPath, sizeof(fullPath), "%s/scenes/%s.drsc", hubProjectDir, cleanName);

            // Build a SceneData from the live archetype and save it
            SceneData sd = {0};
            sd.archetypeCount = 1;
            sd.archetypes = &sceneArchetype;
            strncpy(sd.archetypeNames[0], "SceneEntity", MAX_SCENE_NAME - 1);
            // Make sure the arena knows the live entity count
            sceneArchetype.arena[0].count = entityCount;
            sceneArchetype.cachedEntityCount = entityCount;
            if (entityCount > 0 && sceneArchetype.activeChunkCount == 0)
                sceneArchetype.activeChunkCount = 1;
            // Sync material data
            sd.materialCount = resources->materialUsed;
            sd.materials     = resources->materialBuffer;

            if (saveScene(fullPath, &sd))
            {
                INFO("Scene saved to %s", fullPath);
            }
            else
            {
                ERROR("Failed to save scene to %s", fullPath);
            }


            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (showLoadModal)
    {
        // Scan for .drsc files when opening the modal
        ImGui::OpenPopup("Load Scene");
        showLoadModal = false;
    }
    if (ImGui::BeginPopupModal("Load Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Scan scenes directory for .drsc files
        static c8 **sceneFiles = nullptr;
        static u32 sceneFileCount = 0;
        static b8 needsScan = true;

        if (needsScan)
        {
            // Free previous scan
            if (sceneFiles)
            {
                for (u32 i = 0; i < sceneFileCount; i++)
                    free(sceneFiles[i]);
                free(sceneFiles);
                sceneFiles = nullptr;
            }
            sceneFileCount = 0;

            c8 scenesDir[512];
            snprintf(scenesDir, sizeof(scenesDir), "%s/scenes", hubProjectDir);

            u32 totalFiles = 0;
            c8 **allFiles = listFilesInDirectory(scenesDir, &totalFiles);

            // Filter for .drsc extension
            if (allFiles && totalFiles > 0)
            {
                sceneFiles = (c8 **)malloc(sizeof(c8 *) * totalFiles);
                for (u32 i = 0; i < totalFiles; i++)
                {
                    u32 len = (u32)strlen(allFiles[i]);
                    if (len > 5 && strcmp(allFiles[i] + len - 5, ".drsc") == 0)
                    {
                        sceneFiles[sceneFileCount] = allFiles[i];
                        sceneFileCount++;
                    }
                    else
                    {
                        free(allFiles[i]);
                    }
                }
                free(allFiles);
            }
            needsScan = false;
        }

        ImGui::Text("Available Scenes:");
        ImGui::Separator();
        if (sceneFileCount == 0)
        {
            ImGui::TextDisabled("No .drsc files found in scenes/");
        }
        else
        {
            for (u32 i = 0; i < sceneFileCount; i++)
            {
                // Show just the filename
                const c8 *name = sceneFiles[i];
                const c8 *slash = strrchr(name, '/');
                if (!slash) slash = strrchr(name, '\\');
                const c8 *display = slash ? slash + 1 : name;

                if (ImGui::Selectable(display))
                {
                    strncpy(scenePathBuffer, sceneFiles[i], sizeof(scenePathBuffer) - 1);
                    scenePathBuffer[sizeof(scenePathBuffer) - 1] = '\0';
                }
            }
        }
        ImGui::Separator();
        ImGui::InputText("Path", scenePathBuffer, sizeof(scenePathBuffer));
        if (ImGui::Button("Load"))
        {
            editorLoadSceneFile(scenePathBuffer);
            needsScan = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { needsScan = true; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (showNewSceneModal)
    {
        ImGui::OpenPopup("New Scene");
        showNewSceneModal = false;
    }
    if (ImGui::BeginPopupModal("New Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static i32 newCapacity = 128;
        ImGui::InputInt("Initial Capacity", &newCapacity);
        if (newCapacity < 1) newCapacity = 1;
        if (ImGui::Button("Create"))
        {
            createBlankScene((u32)newCapacity);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void editorLog(LogLevel level, const c8 *msg)
{
    if (!consoleLines) return;
    if (g_consoleCount >= MAX_CONSOLE_LINES) return;
    consoleLines[g_consoleCount] = strdup(msg);
    g_consoleCount++;
}

