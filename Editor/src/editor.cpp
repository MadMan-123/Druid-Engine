
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
#include <iostream>

#include "../deps/imgui/imgui_internal.h"
#include "entitypicker.h"

// SceneEntity archetype definition (moved from druid.h)
DEFINE_ARCHETYPE(SceneEntity,
    FIELD(Vec3, position),
    FIELD(Vec4, rotation),
    FIELD(Vec3, scale),
    FIELD(b8, isActive),
    FIELD(c8[MAX_NAME_SIZE], name),
    FIELD(u32, modelID),
    FIELD(u32, shaderHandle),
    FIELD(u32, materialID),
    FIELD(u32, archetypeID),   // index into g_archRegistry (which archetype type)
    FIELD(b8, isSceneCamera),
    FIELD(u32, ecsSlotID)      // runtime slot index within the ECS archetype's SoA buffer
);

// Editor-side registry of user-created archetypes
static ArchetypeRegistry g_archRegistry = {0};

// buffer of 2D array of strings
const c8 **consoleLines = NULL;

// Allocate the storage here
Application *editor = nullptr;

// UI state for scene menu modals
c8 scenePathBuffer[512] = "";
static b8 showSaveModal = false;
static b8 showLoadModal = false;
static b8 showNewSceneModal = false;
static b8 showProfiler = false;
static b8 showScenesPanel = true;
b8 showSkyboxSettings = false;

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

    // Clear any previous error on success
    g_skyboxError[0] = '\0';
    INFO("Skybox reloaded");
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

// entity data
u32 entityCount = 0;
InspectorState currentInspectorState =
    EMPTY_VIEW; // set inital inspector view to be empty

u32 inspectorEntityID =
    0; // holds the index for the inspector to load component data

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

// Scanned field storage — populated by scanProjectArchetypes so the registry
// has valid layout.fields pointers even before the DLL is loaded.
static FieldInfo g_scannedFields[MAX_ARCHETYPE_SYSTEMS][32];
static c8        g_scannedFieldNames[MAX_ARCHETYPE_SYSTEMS][32][128];

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
        sysArch->isSingle     = g_archRegistry.entries[a].isSingle;
        sysArch->isBuffered   = g_archRegistry.entries[a].isBuffered;
        sysArch->poolCapacity = g_archRegistry.entries[a].poolCapacity;
        sysArch->isPersistent = g_archRegistry.entries[a].isPersistent;
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

            sysArch->arena[0].count++;
            idx++;
        }
        g_ecsArchEntityCount[a] = matchCount;

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

static void renderGameScene()
{
    // Ensure framebuffer exists before rendering
    if (viewportFBs[activeFBO].fbo == 0 || viewportWidth == 0 || viewportHeight == 0)
    {
        // Framebuffer not initialized yet, skip rendering
        return;
    }

    f32 dt = (f32)(1.0 / (editor->fps > 0.0 ? editor->fps : 60.0));

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
    {
        // In play mode, let the game plugin update camera before UBO push.
        // The game has a Camera* into the renderer buffer and can modify it.
        if (g_gameRunning && g_gameDLL.loaded)
            g_gameDLL.plugin.render(dt);

        rendererBeginFrame(renderer, dt);
    }

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
        for (u32 id = 0; id < entityCount; id++)
        {
            if (!isActive[id]) continue;
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

            for (u32 i = 0; i < model->meshCount; i++)
            {
                u32 meshIndex = model->meshIndices[i];
                if (meshIndex >= resources->meshUsed) continue;

                Mesh *mesh = &resources->meshBuffer[meshIndex];
                if (!mesh || mesh->vao == 0) continue;

                u32 materialIndex = model->materialIndices[i];
                if (entityMaterialIDs && entityMaterialIDs[id] != (u32)-1)
                    materialIndex = entityMaterialIDs[id];
                if (materialIndex >= resources->materialUsed) continue;

                Material *material = &resources->materialBuffer[materialIndex];
                u32 entityShader = shaderHandles[id];
                u32 shaderToUse = (entityShader != 0) ? entityShader : shader;
                glUseProgram(shaderToUse);
                updateShaderModel(shaderToUse, newTransform);
                MaterialUniforms uniforms = getMaterialUniforms(shaderToUse);
                updateMaterial(material, &uniforms);
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
            if (g_ecsArchEntityCount[a] == 0 && !g_ecsArchetypes[a].isBuffered) continue;
            if (g_ecsSystemLoaded[a] && g_ecsSystems[a].render)
                g_ecsSystems[a].render(&g_ecsArchetypes[a], renderer);
            else
                rendererDefaultArchetypeRender(&g_ecsArchetypes[a], renderer);
        }
        } // PROFILE_SCOPE("ECS Render")

        // End deferred geometry pass, run lighting, then draw skybox on top
        if (deferred)
        {
            rendererEndDeferredPass(renderer);

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

        unbindFramebuffer();
        glDisable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        return;
    }

    // ── Edit mode: render editor scene + gizmos ──

    // skybox
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(skyboxShader);

    // Skybox now uses UBO data - no need for individual uniforms
    glBindVertexArray(skyboxMesh->vao);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // draw each scene entity
    Transform newTransform = {0};
    for (u32 id = 0; id < entityCount; id++)
    {
        if (!isActive[id])
            continue;

        // get transform ready
        newTransform = {positions[id], rotations[id], scales[id]};

        // Get the mesh name for this specific entity
      
        // draw the model
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
            
            for (u32 i = 0; i < model->meshCount; i++)
            {
                u32 meshIndex = model->meshIndices[i];
                
                // Check mesh index bounds
                if (meshIndex >= resources->meshUsed)
                {
                    ERROR("Invalid mesh index in editor rendering: entity=%d, model=%d, mesh=%d/%d", id, modelID, meshIndex, resources->meshUsed);
                    continue;
                }
                
                Mesh* mesh = &resources->meshBuffer[meshIndex];
                if (!mesh || mesh->vao == 0)
                {
                    ERROR("Invalid mesh in editor rendering: entity=%d, model=%d, meshIndex=%d (vao=%d)", id, modelID, meshIndex, mesh ? mesh->vao : 0);
                    continue;
                }
                
                // Prefer per-entity material override if set, otherwise use model's material
                u32 materialIndex = model->materialIndices[i];
                if (entityMaterialIDs && entityMaterialIDs[id] != (u32)-1) 
                {
                    materialIndex = entityMaterialIDs[id];
                }
                if (materialIndex >= resources->materialUsed) 
                {
                    ERROR("Invalid material index in editor rendering: entity=%d, material=%d/%d", id, materialIndex, resources->materialUsed);
                    continue;
                }
                Material* material = &resources->materialBuffer[materialIndex];

                // Prefer per-entity shader handle if set, otherwise use global default shader
                u32 entityShader = shaderHandles[id];
                u32 shaderToUse = (entityShader != 0) ? entityShader : shader;
                glUseProgram(shaderToUse);
                updateShaderModel(shaderToUse, newTransform);  // Only set model matrix
                MaterialUniforms uniforms = getMaterialUniforms(shaderToUse);
                updateMaterial(material, &uniforms);
                drawMesh(mesh);
            }
        }
        else
        {
            // modelID out of range – entity has no valid model, skip silently
            continue;
        }
    }

    if (manipulateTransform)
    {
        Vec3 pos = positions[inspectorEntityID];

        // Scale gizmos based on distance from camera so they stay a constant screen size
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

        // Draw gizmos on top of everything (no depth test)
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

    // Ensure we're in a clean OpenGL state before unbinding
    unbindFramebuffer();

    // Reset OpenGL state to defaults
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
        ImGui::GetWindowPos(); // screen position of the window
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

    resizeViewportFramebuffers((i32)targetW, (i32)targetH);
    // Keep the ID framebuffer sized to the viewport so picking reads use correct coords
    resizeIDFramebuffer((i32)targetW, (i32)targetH);

    ImVec2 cursor = ImGui::GetCursorPos();
    ImVec2 imageOffset =
        ImVec2((avail.x - targetW) * 0.5f, (avail.y - targetH) * 0.5f);
    ImGui::SetCursorPos(
        ImVec2(cursor.x + imageOffset.x, cursor.y + imageOffset.y));

    // Save image position for mouse picking — must include the content region
    // offset (title bar + padding) so viewport-relative coords are correct.
    g_viewportScreenPos = ImGui::GetCursorScreenPos();
    g_viewportSize = ImVec2(targetW, targetH);

    // update camera projection
    sceneCam.projection =
        mat4Perspective(radians(70.0f), targetAspect, 0.1f, 100.0f);

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

    // Debug prints
    // DEBUG("Viewport Image Pos: (%.2f, %.2f)\n", g_viewportScreenPos.x,
    // g_viewportScreenPos.y); DEBUG("Viewport Image Size: (%.2f x %.2f)\n",
    // g_viewportSize.x, g_viewportSize.y);
}

static void drawDebugWindow()
{
    const c8 *inspectorStateNames[] = {"EMPTY_VIEW", "ENTITY_VIEW", "SKYBOX_VIEW"};
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
    ImGui::Text("Inspector State: %d",
                inspectorStateNames[currentInspectorState]);
    ImGui::Text("Viewport Size: %.0f x %.0f", viewportWidth, viewportHeight);
    
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

    // draw console line
    u32 dummy = -1; // No selection
    // ImGui::ListBox("Console", &dummy, consoleLines, MAX_CONSOLE_LINES, 10);
    ImGui::End();
}

static void drawConsoleWindow()
{
    ImGui::Begin("Output");

    if (ImGui::Button("Clear"))
    {
        for (u32 i = 0; i < MAX_CONSOLE_LINES; i++)
        {
            if (consoleLines[i])
            {
                free((void *)consoleLines[i]);
                consoleLines[i] = NULL;
            }
        }
    }
    ImGui::SameLine();
    static b8 autoScroll = true;
    ImGui::Checkbox("Auto-scroll", (bool *)&autoScroll);
    ImGui::Separator();

    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (u32 i = 0; i < MAX_CONSOLE_LINES; i++)
    {
        if (!consoleLines[i]) break;

        const c8 *line = consoleLines[i];
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

        // only look at .c files
        if (plen < 3 || strcmp(path + plen - 2, ".c") != 0)
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

        // Look for "FieldInfo <name>_fields[]"
        const c8 *marker = strstr(buf, "FieldInfo ");
        if (!marker || !strstr(marker, "_fields[]"))
        {
            free(buf);
            free(files[fi]);
            continue;
        }

        // Extract archetype name: between "FieldInfo " and "_fields[]"
        const c8 *nameStart = marker + strlen("FieldInfo ");
        const c8 *nameEnd   = strstr(nameStart, "_fields[]");
        if (!nameEnd || nameEnd <= nameStart)
        {
            free(buf);
            free(files[fi]);
            continue;
        }

        c8 archName[MAX_SCENE_NAME] = {0};
        u32 nameLen = (u32)(nameEnd - nameStart);
        if (nameLen >= MAX_SCENE_NAME) nameLen = MAX_SCENE_NAME - 1;
        memcpy(archName, nameStart, nameLen);
        archName[nameLen] = '\0';

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

        // Parse { "name", sizeof(type) } entries and store field info
        u32 fieldCount = 0;
        const c8 *scan = strstr(buf, "_fields[] = {");
        if (scan)
        {
            scan = strchr(scan, '{');
            if (scan) scan++; // skip the opening {

            while (scan && fieldCount < 32)
            {
                // find next { "
                const c8 *entryStart = strstr(scan, "{ \"");
                if (!entryStart) break;
                // make sure we haven't passed the closing };
                const c8 *closeBrace = strstr(scan, "};");
                if (closeBrace && entryStart > closeBrace) break;

                // Extract field name between quotes
                const c8 *ns = entryStart + 3;
                const c8 *ne = strchr(ns, '"');
                if (ne && (u32)(ne - ns) < 128)
                {
                    memset(g_scannedFieldNames[regIdx][fieldCount], 0, 128);
                    memcpy(g_scannedFieldNames[regIdx][fieldCount], ns, (u32)(ne - ns));
                }

                // Extract type from sizeof(Type)
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

                // advance past this entry
                const c8 *entryEnd = strchr(scan, '}');
                if (entryEnd) scan = entryEnd + 1;
                else break;
            }
        }

        entry->layout.count  = fieldCount;
        entry->layout.fields = g_scannedFields[regIdx];

        // Check for isSingle (look for "isSingle" in the comment block or source)
        if (strstr(buf, "isSingle"))
            entry->isSingle = true;

        // Check for isBuffered
        if (strstr(buf, "isBuffered"))
            entry->isBuffered = true;

        // Scan for POOL_CAPACITY define
        {
            const char *pcDef = strstr(buf, "#define POOL_CAPACITY ");
            if (pcDef) entry->poolCapacity = (u32)atoi(pcDef + 22);
        }

        // Detect uniform scale: field named "Scale" with sizeof(f32)
        if (fieldCount >= 3
            && strcmp(g_scannedFieldNames[regIdx][2], "Scale") == 0
            && g_scannedFields[regIdx][2].size == sizeof(f32))
            entry->uniformScale = true;

        g_archRegistry.count++;
        INFO("Scanned archetype: %s (%u fields) from %s", archName, fieldCount, path);

        free(buf);
        free(files[fi]);
    }

    free(files);
}

static void drawPrefabsWindow()
{
    // ---- archetype designer state (persists across frames) ----
    static c8  archName[128]           = "";
    static bool archIsSingle           = false;
    static bool archIsPersistent       = false;
    static bool archIsBuffered         = false;
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
    ImGui::Checkbox("isSingle", &archIsSingle);
    ImGui::SameLine();
    ImGui::Checkbox("isPersistent", &archIsPersistent);
    ImGui::SameLine();
    ImGui::Checkbox("isBuffered", &archIsBuffered);
    
    if (archIsBuffered)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderInt("Pool Size", &archBufferSize, 10, 1000);
        ImGui::TextDisabled("Buffered archetypes create a pool with hidden instances (Alive flag)");
    }

    // Transform is always included; offer uniform scale option
    static bool archUniformScale = false;
    ImGui::Checkbox("Uniform Scale (f32)", &archUniformScale);
    ImGui::TextDisabled("Transform auto-included: Position(Vec3), Rotation(Vec4), Scale(%s)",
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
        // Prepend transform fields: Position(Vec3), Rotation(Vec4), Scale(Vec3|f32)
        static c8 tfNames[3][128] = {"Position", "Rotation", "Scale"};
        const u32 tfCount = 3;
        const u32 totalFields = tfCount + fieldCount;

        FieldInfo allFields[ARCH_MAX_FIELDS + 3];
        const c8 *allTypes[ARCH_MAX_FIELDS + 3];

        allFields[0] = { tfNames[0], sizeof(Vec3) }; allTypes[0] = "Vec3";
        allFields[1] = { tfNames[1], sizeof(Vec4) }; allTypes[1] = "Vec4";
        if (archUniformScale) {
            allFields[2] = { tfNames[2], sizeof(f32) }; allTypes[2] = "f32";
        } else {
            allFields[2] = { tfNames[2], sizeof(Vec3) }; allTypes[2] = "Vec3";
        }

        for (u32 i = 0; i < fieldCount; i++)
        {
            allFields[tfCount + i].name = fieldNames[i];
            allFields[tfCount + i].size = typeSizes[fieldTypeIndex[i]];
            allTypes[tfCount + i]       = typeNames[fieldTypeIndex[i]];
        }

        if (generateArchetypeFiles(hubProjectDir, archName, allFields, allTypes, totalFields, archIsSingle, archIsBuffered, (u32)archBufferSize))
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
                snprintf(entry->sourcePath, MAX_PATH_LENGTH, "src/%s.c", archName);
                entry->isSingle      = archIsSingle;
                entry->isPersistent  = archIsPersistent;
                entry->uniformScale  = archUniformScale;
                entry->isBuffered    = archIsBuffered;
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

    // ---- Registered Archetypes list ----
    ImGui::Separator();
    ImGui::Text("Registered Archetypes (%u)", g_archRegistry.count);

    if (g_archRegistry.count > 0)
    {
        for (u32 a = 0; a < g_archRegistry.count; a++)
        {
            ArchetypeFileEntry *entry = &g_archRegistry.entries[a];
            ImGui::PushID((int)a + 1000);

            ImGui::BulletText("%s  (%u fields%s%s%s)",
                entry->name,
                entry->layout.count,
                entry->isSingle ? ", single" : "",
                entry->isPersistent ? ", persistent" : "",
                entry->isBuffered ? ", buffered" : "");

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

                // NOTE: layout.fields always points into g_scannedFields (a static
                // array) — never heap-allocated — so do NOT call free() on it.
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
                archIsSingle     = entry->isSingle;
                archIsPersistent = entry->isPersistent;
                archIsBuffered   = entry->isBuffered;
                archBufferSize   = entry->poolCapacity > 0 ? (i32)entry->poolCapacity : 100;

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
                                const c8 *es = strstr(scan, "{ \"");
                                if (!es) break;
                                const c8 *closeBr = strstr(scan, "};");
                                if (closeBr && es > closeBr) break;

                                const c8 *ns = es + 3;
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

                        if (allFieldCount >= tfStart + 3
                            && strcmp(allFieldNames[tfStart + 0], "Position") == 0
                            && strcmp(allFieldNames[tfStart + 1], "Rotation") == 0
                            && strcmp(allFieldNames[tfStart + 2], "Scale")    == 0)
                        {
                            skipCount    = tfStart + 3;
                            // f32 is index 0 in typeNames[]
                            archUniformScale = (allFieldTypeIdx[tfStart + 2] == 0);
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
                    static c8 rgTfNames[3][128] = {"Position", "Rotation", "Scale"};
                    const u32 rgTfCount = 3;
                    const u32 rgTotal = rgTfCount + fieldCount;

                    FieldInfo rFields[ARCH_MAX_FIELDS + 3];
                    const c8 *rTypes[ARCH_MAX_FIELDS + 3];

                    rFields[0] = { rgTfNames[0], sizeof(Vec3) }; rTypes[0] = "Vec3";
                    rFields[1] = { rgTfNames[1], sizeof(Vec4) }; rTypes[1] = "Vec4";
                    if (archUniformScale) {
                        rFields[2] = { rgTfNames[2], sizeof(f32) }; rTypes[2] = "f32";
                    } else {
                        rFields[2] = { rgTfNames[2], sizeof(Vec3) }; rTypes[2] = "Vec3";
                    }
                    for (u32 i = 0; i < fieldCount; i++)
                    {
                        rFields[rgTfCount + i].name = fieldNames[i];
                        rFields[rgTfCount + i].size = typeSizes[fieldTypeIndex[i]];
                        rTypes[rgTfCount + i]       = typeNames[fieldTypeIndex[i]];
                    }
                    if (generateArchetypeFiles(hubProjectDir, archName, rFields, rTypes, rgTotal, archIsSingle, archIsBuffered, (u32)archBufferSize))
                    {
                        entry->layout.count  = rgTotal;
                        entry->isSingle      = archIsSingle;
                        entry->isPersistent  = archIsPersistent;
                        entry->uniformScale  = archUniformScale;
                        entry->isBuffered    = archIsBuffered;
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
        if (prof)
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
        destroyArchetype(&sceneArchetype);

        sceneArchetype  = sd.archetypes[0];
        free(sd.archetypes);
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
            free(sd.materials);
        }

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
            modelIDs[entityCount] = (u32)-1; // Initialize modelID to an invalid index
            entityMaterialIDs[entityCount] = (u32)-1; // no custom material
            shaderHandles[entityCount] = 0; // no custom shader
            archetypeIDs[entityCount] = (u32)-1; // no archetype assigned
            if (ecsSlotIDs)      ecsSlotIDs[entityCount]      = (u32)-1;
            if (sceneCameraFlags) sceneCameraFlags[entityCount] = false;

            // make the inital name this
            sprintf(&names[entityCount * MAX_NAME_SIZE], "Entity %d", entityCount);

            entityCount++;
            DEBUG("Added Entity %d\n", entityCount);
        }
    }

    c8 *entityName;
    for (u32 i = 0; i < entityCount; i++)
    {
        entityName = &names[i * MAX_NAME_SIZE];

        ImGui::PushID(i);
        static c8 buttonLabel[320];
        const c8 *baseLabel = (entityName[0] == '\0') ? "[Unnamed Entity]" : entityName;
        if (sceneCameraFlags && sceneCameraFlags[i])
            snprintf(buttonLabel, sizeof(buttonLabel), "[Camera] %s", baseLabel);
        else
            snprintf(buttonLabel, sizeof(buttonLabel), "%s", baseLabel);

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
        ImGui::Text("Nowt to see here");
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
            ImGui::Text("No entity selected or scene not loaded.");
            break;
        }

        // Sync euler display whenever a different entity is selected
        // (covers both viewport picks and scene-list clicks)
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

        ImGui::InputText("##Name", &names[inspectorEntityID * MAX_NAME_SIZE],
                         MAX_NAME_SIZE);
        // draw the scene entity basic data
        ImGui::DragFloat3("position", (f32 *)&positions[inspectorEntityID], 0.01f);

        if (ImGui::DragFloat3("rotation", (f32 *)&EulerAnglesDegrees, 0.5f))
        {
            Vec3 eulerRadians;
            eulerRadians.x = radians(EulerAnglesDegrees.x);
            eulerRadians.y = radians(EulerAnglesDegrees.y);
            eulerRadians.z = radians(EulerAnglesDegrees.z);
            rotations[inspectorEntityID] = quatFromEuler(eulerRadians);
        }
        ImGui::DragFloat3("scale", (f32 *)&scales[inspectorEntityID], 0.01f);

        ImGui::Separator();
        drawSoAEditorSection(inspectorEntityID);
        ImGui::Separator();

        if (sceneCameraFlags)
        {
            b8 isSceneCamera = sceneCameraFlags[inspectorEntityID];
            if (ImGui::Checkbox("Scene Camera", (bool *)&isSceneCamera))
            {
                if (isSceneCamera)
                {
                    setSceneCameraEntity(inspectorEntityID);
                    applySceneCameraEntityToSceneCam();
                }
                else
                {
                    sceneCameraFlags[inspectorEntityID] = false;
                }
            }

            if (sceneCameraFlags[inspectorEntityID])
            {
                if (ImGui::Button("Move View To Camera"))
                    applySceneCameraEntityToSceneCam();
                ImGui::SameLine();
                if (ImGui::Button("Match Camera To View"))
                {
                    positions[inspectorEntityID] = sceneCam.pos;
                    rotations[inspectorEntityID] = sceneCam.orientation;
                }
            }
        }

        // ---- Archetype assignment ----
        if (archetypeIDs)
        {
            u32 currentArchID = archetypeIDs[inspectorEntityID];
            const c8 *archPreview = (currentArchID < g_archRegistry.count)
                ? g_archRegistry.entries[currentArchID].name : "None";

            if (ImGui::BeginCombo("Archetype", archPreview))
            {
                // "None" option
                if (ImGui::Selectable("None", currentArchID == (u32)-1))
                    archetypeIDs[inspectorEntityID] = (u32)-1;

                for (u32 a = 0; a < g_archRegistry.count; a++)
                {
                    bool selected = (currentArchID == a);
                    if (ImGui::Selectable(g_archRegistry.entries[a].name, selected))
                        archetypeIDs[inspectorEntityID] = a;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // ---- Archetype field values ----
            if (currentArchID < g_archRegistry.count)
            {
                ArchetypeFileEntry *archEntry = &g_archRegistry.entries[currentArchID];
                ImGui::Separator();
                ImGui::Text("Archetype: %s", archEntry->name);

                // During play mode: show live values from the per-system archetype
                if (g_gameRunning && g_ecsSystemLoaded[currentArchID]
                    && g_ecsArchEntityCount[currentArchID] > 0
                    && g_ecsArchetypes[currentArchID].arena)
                {
                    // Find this entity's index within the per-system archetype
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
                                if (sz == sizeof(f32))
                                    ImGui::DragFloat(layout->fields[f].name, (f32 *)ptr, 0.01f);
                                else if (sz == sizeof(Vec3))
                                    ImGui::DragFloat3(layout->fields[f].name, (f32 *)ptr, 0.01f);
                                else if (sz == sizeof(Vec4))
                                    ImGui::DragFloat4(layout->fields[f].name, (f32 *)ptr, 0.01f);
                                else if (sz == sizeof(Vec2))
                                    ImGui::DragFloat2(layout->fields[f].name, (f32 *)ptr, 0.01f);
                                else
                                    ImGui::Text("%s (%u bytes)", layout->fields[f].name, sz);
                                ImGui::PopID();
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("Entity not in this archetype's runtime set");
                    }
                }
                else
                {
                    // Edit mode or system not loaded: show field names from scanner
                    StructLayout *layout = &archEntry->layout;
                    if (layout->fields)
                    {
                        for (u32 f = 0; f < layout->count; f++)
                        {
                            const c8 *fname = layout->fields[f].name;
                            u32 fsz = layout->fields[f].size;
                            if (fsz > 0)
                                ImGui::TextDisabled("  %s  (%u bytes)", fname, fsz);
                            else
                                ImGui::TextDisabled("  %s", fname);
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("  %u fields (start play to inspect)", layout->count);
                    }
                }
            }
        }

        u32 currentModelID = modelIDs[inspectorEntityID];
        u32 selectedIndex = (currentModelID < resources->modelUsed) ? currentModelID : 0;
        
        // Show warning if entity has no model assigned
        if (currentModelID == (u32)-1)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No model assigned to this entity");
        }
        
        if (ImGui::BeginListBox("Models"))
        {
            // Add "None" option for no model
            b8 isNoneSelected = (currentModelID == (u32)-1);
            if(ImGui::Selectable("None", isNoneSelected))
            {
                modelIDs[inspectorEntityID] = (u32)-1;
            }
            
            for(u32 modelIdx = 0; modelIdx < resources->modelUsed; modelIdx++)
            {
                const b8 isSelected = (currentModelID == modelIdx);
                Model *model = &resources->modelBuffer[modelIdx];
                
                // Create unique ID for each model selectable
                ImGui::PushID(modelIdx);
                if(ImGui::Selectable(model->name, isSelected))
                {
                    modelIDs[inspectorEntityID] = modelIdx;
                }
                ImGui::PopID();
            }
        }
        
        


        ImGui::EndListBox();
        
        //list the material data 
        u32 modelID = modelIDs[inspectorEntityID];
        if (modelID < resources->modelUsed)
        {
            Model *model = &resources->modelBuffer[modelID];
            if (model->meshCount > 0)
            {
                // Determine default and effective material indices
                u32 defaultMaterialIndex = model->materialIndices[0];
                u32 effectiveMaterialIndex = defaultMaterialIndex;
                if (entityMaterialIDs && entityMaterialIDs[inspectorEntityID] != (u32)-1) {
                    effectiveMaterialIndex = entityMaterialIDs[inspectorEntityID];
                }
                // guard
                if (effectiveMaterialIndex >= resources->materialUsed) {
                    ERROR("Invalid material index for inspector: %u (used=%u)", effectiveMaterialIndex, resources->materialUsed);
                    break;
                }

                Material *mat = &resources->materialBuffer[effectiveMaterialIndex];

                b8 isCustom = (effectiveMaterialIndex != defaultMaterialIndex);

                ImGui::Text("Material");
                ImGui::SameLine();
                if (isCustom) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "(Custom)");
                    ImGui::SameLine();
                }

                // Create a custom material clone for this entity
                if (ImGui::Button("Make Custom Material"))
                {
                    if (!resources)
                    {
                        WARN("Resource manager not initialized");
                    }
                    else if (resources->materialUsed >= resources->materialCount)
                    {
                        WARN("Cannot create custom material: material buffer full");
                    }
                    else
                    {
                        u32 newIndex = resources->materialUsed;
                        // copy the model's default material into resource manager (so custom starts from default)
                        resources->materialBuffer[newIndex] = resources->materialBuffer[defaultMaterialIndex];

                        // generate a name for the material
                        c8 newName[256];
                        const c8 *entityName = &names[inspectorEntityID * MAX_NAME_SIZE];
                        if (entityName == NULL || entityName[0] == '\0')
                            entityName = model->name;
                        snprintf(newName, sizeof(newName), "%s-custom-%u", entityName, newIndex);

                        insertMap(&resources->materialIDs, newName, &resources->materialUsed);
                        resources->materialUsed++;
                        // assign to entity
                        if (entityMaterialIDs)
                            entityMaterialIDs[inspectorEntityID] = newIndex;

                        INFO("Created custom material '%s' at index %u for entity %u", newName, newIndex, inspectorEntityID);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Custom Material"))
                {
                    if (entityMaterialIDs)
                    {
                        entityMaterialIDs[inspectorEntityID] = (u32)-1;
                        INFO("Cleared custom material for entity %u", inspectorEntityID);
                        // recompute effective material to default
                        effectiveMaterialIndex = defaultMaterialIndex;
                        mat = &resources->materialBuffer[effectiveMaterialIndex];
                    }
                }
                // If we just created a custom material above, refresh mat/effective index
                if (entityMaterialIDs && entityMaterialIDs[inspectorEntityID] != (u32)-1) {
                    effectiveMaterialIndex = entityMaterialIDs[inspectorEntityID];
                    if (effectiveMaterialIndex < resources->materialUsed)
                        mat = &resources->materialBuffer[effectiveMaterialIndex];
                }

                drawTextureSelector("Albedo", &mat->albedoTex, "##Select Albedo");
                drawTextureSelector("Normal", &mat->normalTex, "##Select Normal");
                drawTextureSelector("Metallic", &mat->metallicTex, "##Select Metallic");
                drawTextureSelector("Roughness", &mat->roughnessTex, "##Select Roughness");

                ImGui::ColorEdit3("Colour", (f32 *)&mat->colour);
                ImGui::SliderFloat("Metallic", &mat->metallic, 0.0f, 1.0f); 
                ImGui::SliderFloat("Roughness", &mat->roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("Transparency", &mat->transparency, 0.0f, 1.0f);

                // Shader selection
                const c8* currentShaderName = "None";
                // Find the name of the current shader
                for (u32 shaderNameIdx = 0; shaderNameIdx < resources->shaderIDs.capacity; shaderNameIdx++) {
                    if (resources->shaderIDs.pairs[shaderNameIdx].occupied) {
                        u32 shaderIndex = *(u32*)resources->shaderIDs.pairs[shaderNameIdx].value;
                        if (resources->shaderHandles[shaderIndex] == shaderHandles[inspectorEntityID]) {
                            currentShaderName = (const c8*)resources->shaderIDs.pairs[shaderNameIdx].key;
                            break;
                        }
                    }
                }

                if (ImGui::BeginCombo("Shader", currentShaderName))
                {
                    for (u32 shaderIdx = 0; shaderIdx < resources->shaderIDs.capacity; shaderIdx++)
                    {
                        if (resources->shaderIDs.pairs[shaderIdx].occupied)
                        {
                            const c8* shaderName = (const c8*)resources->shaderIDs.pairs[shaderIdx].key;
                            u32 shaderIndex = *(u32*)resources->shaderIDs.pairs[shaderIdx].value;
                            u32 shaderHandle = resources->shaderHandles[shaderIndex];

                            const b8 is_selected = (shaderHandles[inspectorEntityID] == shaderHandle);
                            if (ImGui::Selectable(shaderName, is_selected))
                            {
                                shaderHandles[inspectorEntityID] = shaderHandle;
                            }
                            if (is_selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }

        // material
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

static b8 showBuildLog = false;

static void drawBuildLogWindow()
{
    if (!showBuildLog) return;

    ImGui::Begin("Build Log", &showBuildLog);
    if (g_buildInProgress)
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Building...");
    else
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Idle");

    ImGui::Separator();
    ImGui::BeginChild("##logscroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(g_buildLog);
    ImGui::EndChild();
    ImGui::End();
}

// temporary file used to snapshot the scene before play-mode
static const c8 *PLAY_SNAPSHOT_PATH = "__play_snapshot__.drsc";
static b8 g_hasPlaySnapshot = false;

static void snapshotScene()
{
    // build a SceneData from the live archetype
    SceneData sd = {0};
    sd.archetypeCount = 1;
    sd.archetypes = &sceneArchetype;
    strncpy(sd.archetypeNames[0], "SceneEntity", MAX_SCENE_NAME - 1);
    sceneArchetype.arena[0].count = entityCount;
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
        destroyArchetype(&sceneArchetype);
        sceneArchetype  = sd.archetypes[0];
        free(sd.archetypes); // free outer array; arena/layout now owned by sceneArchetype
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
            free(sd.materials);
        }

        inspectorEntityID     = 0;
        currentInspectorState = EMPTY_VIEW;
        manipulateTransform   = false;
        INFO("Scene restored from play-mode snapshot");
    }
    else
    {
        WARN("Failed to restore scene from snapshot");
    }

    // clean up the temp file
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
#ifdef _WIN32
    snprintf(dllPath, sizeof(dllPath), "%s/bin/libgame.dll", hubProjectDir);
#else
    snprintf(dllPath, sizeof(dllPath), "%s/bin/libgame.so", hubProjectDir);
#endif

    if (!loadGameDLL(dllPath, &g_gameDLL))
    {
        ERROR("Failed to load game DLL: %s", dllPath);
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
            
            
            //if (sceneManager && sceneManager->sceneCount > 0)
            // {
            //     // List existing scenes for quick switch/remove
            //     for (u32 i = 0; i < sceneManager->sceneCount; i++)
            //     {
            //         c8 label[64];
            //         snprintf(label, sizeof(label), "Scene %u", i);
            //         if (ImGui::MenuItem(label, NULL, (i32)(sceneManager->currentScene == i)))
            //         {
            //             // switch scene
            //             switchScene(sceneManager, i);
            //         }
            //         ImGui::SameLine();
            //         ImGui::PushID(i);
            //         if (ImGui::SmallButton("Remove"))
            //         {
            //             removeScene(sceneManager, i);
            //         }
            //         ImGui::PopID();
            //     }
            //}
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

    // build default layout once
    static b8 first_time = true;
    if (first_time)
    {
        first_time = false;
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

    //--actual docked windows--
    drawViewportWindow();
    drawDebugWindow();
    drawPrefabsWindow();
    drawScenesPanel();
    drawSceneListWindow();
    drawInspectorWindow();
    drawProfilerWindow();
    drawSkyboxSettingsWindow();
    drawBuildLogWindow();
    drawConsoleWindow();

    // tick game plugin + ECS systems
    if (g_gameRunning && g_gameDLL.loaded)
    {
        f32 dt = (f32)(1.0 / (editor->fps > 0.0 ? editor->fps : 60.0));
        g_gameDLL.plugin.update(dt);

        // Run ECS system update callbacks on per-system archetypes
        {
        PROFILE_SCOPE("ECS Update");
        for (u32 a = 0; a < g_archRegistry.count && a < MAX_ARCHETYPE_SYSTEMS; a++)
        {
            if (!g_ecsSystemLoaded[a] || !g_ecsSystems[a].update) continue;
            // Buffered archetypes always tick (pool spawn can add entities at runtime)
            if (g_ecsArchEntityCount[a] > 0 || g_ecsArchetypes[a].isBuffered)
                g_ecsSystems[a].update(&g_ecsArchetypes[a], dt);
        }
        } // PROFILE_SCOPE("ECS Update")

        // Sync ECS-updated data (position, rotation, …) back to the scene
        // archetype so the editor renderer draws entities correctly.
        syncEcsToScene();
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
    for (u32 i = 0; i < MAX_CONSOLE_LINES; i++)
    {
        if (consoleLines[i] == NULL)
        {
            consoleLines[i] = strdup(msg); // duplicate the message string
            break;
        }
    }
}

