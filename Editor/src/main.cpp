#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_impl_opengl3.h"
#include "../deps/imgui/imgui_impl_sdl3.h"
#include "../deps/imgui/imgui_internal.h"
#include "editor.h"
#include "entitypicker.h"
#include "hub.h"
#include "project_builder.h"
#include <druid.h>
#include <iostream>

static SDL_Event evnt;
static b8 g_quitRequested = false;
Mesh *cubeMesh = nullptr;
u32 shader = 0;

f32 yaw = 0;
f32 currentPitch = 0;
f32 camMoveSpeed = 1.0f;
static const f32 camRotateSpeed = 5.0f;
static const u32 entityDefaultCount = 128;
MaterialUniforms materialUniforms = {0};
static u32 g_startupModelRefCount = 0;
static c8 (*g_startupModelRefs)[MAX_NAME_SIZE] = nullptr;

Archetype sceneArchetype;
c8 inputBoxBuffer[100];
i32 entitySize = 0;
// helper – move camera with wasd keys
b8 canMoveViewPort = false;
b8 canMoveAxis = false;
Vec3 manipulateAxis = v3Zero;

// ImGui allocator wrappers
static void *MyMalloc(size_t size, void *)
{
    return malloc(size);
}
static void MyFree(void *ptr, void *)
{
    free(ptr);
}

static void moveCamera(f32 dt)
{
    const f32 deadZone = 0.45f;
    if (yInputAxis > deadZone)
    {
        moveForward(&sceneCam, camMoveSpeed * dt);
    }
    if (yInputAxis < -deadZone)
        moveForward(&sceneCam, -camMoveSpeed * dt);
    if (xInputAxis > deadZone)
        moveRight(&sceneCam, camMoveSpeed * dt);
    if (xInputAxis < -deadZone)
        moveRight(&sceneCam, -camMoveSpeed * dt);
}

void rotateCamera(f32 dt)
{
    Vec2 axis = getJoystickAxis(1, JOYSTICK_RIGHT_X, JOYSTICK_RIGHT_Y);
    if (isMouseDown(SDL_BUTTON_RIGHT))
    {
        // Get the mouse delta
        f32 x, y;
        getMouseDelta(&x, &y);

        // apply the mouse delta to the camera
        yaw += -x * (camRotateSpeed)*dt;
        currentPitch += -y * (camRotateSpeed)*dt;

        // 89 in radians
        f32 goal = radians(89.0f);
        // Clamp pitch to avoid gimbal lock
        currentPitch = clamp(currentPitch, -goal, goal);

        // Create yaw quaternion based on the world-up vector
        Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
        Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
        sceneCam.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
    }
    
    // Arrow key rotation
    if (isKeyDown(KEY_LEFT))
    {
        yaw += camRotateSpeed * dt;
        f32 goal = radians(89.0f);
        currentPitch = clamp(currentPitch, -goal, goal);
        Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
        Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
        sceneCam.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
    }
    if (isKeyDown(KEY_RIGHT))
    {
        yaw -= camRotateSpeed * dt;
        f32 goal = radians(89.0f);
        currentPitch = clamp(currentPitch, -goal, goal);
        Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
        Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
        sceneCam.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
    }
    if (isKeyDown(KEY_UP))
    {
        currentPitch += camRotateSpeed * dt;
        f32 goal = radians(89.0f);
        currentPitch = clamp(currentPitch, -goal, goal);
        Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
        Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
        sceneCam.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
    }
    if (isKeyDown(KEY_DOWN))
    {
        currentPitch -= camRotateSpeed * dt;
        f32 goal = radians(89.0f);
        currentPitch = clamp(currentPitch, -goal, goal);
        Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
        Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
        sceneCam.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
    }
}

void moveViewPortCamera(f32 dt)
{
    // apply user input to the camera
    moveCamera(dt);
    rotateCamera(dt);
    
    // Handle camera move speed with scroll wheel
    ImGuiIO &io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f)
    {
        camMoveSpeed += io.MouseWheel * 0.1f;
        camMoveSpeed = clamp(camMoveSpeed, 1.0f, 10.0f);
    }
}
void processInput(void *appData)
{
    // void* should be Application
    Application *app = (Application *)appData;
    // tell SDL to process events
    SDL_PumpEvents();

    while (SDL_PollEvent(&evnt)) // get and process events
    {
        // pass imgui events
        ImGui_ImplSDL3_ProcessEvent(&evnt);
        switch (evnt.type)
        {
        // if the quit event is triggered then change the state to exit
        case SDL_EVENT_QUIT:
            g_quitRequested = true;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            checkForGamepadConnection(&evnt);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            checkForGamepadRemoved(&evnt);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        {
            i32 w = evnt.window.data1;
            i32 h = evnt.window.data2;
            if (w > 0 && h > 0)
            {
                app->width  = (f32)w;
                app->height = (f32)h;
                glViewport(0, 0, w, h);
            }
            break;
        }
        default:;
        }
    }

    ImGuiIO &io = ImGui::GetIO();
    b8 capture = (b8)(io.WantCaptureMouse || io.WantCaptureKeyboard);

    // Let input through whenever the viewport is hovered so camera WASD/mouse
    // and gizmo dragging work in both edit and play mode.
    if (canMoveViewPort)
        capture = false;

    // In play mode the game/plugin should always receive raw input.
    if (g_gameRunning)
        capture = false;

    setInputCaptureState(capture);
}

void reallocateSceneArena()
{
}

void rebindArchetypeFields()
{
    void **fields = getArchetypeFields(&sceneArchetype, 0);
    if (!fields)
    {
        FATAL("Failed to rebind archetype fields");
        return;
    }
    u32 fieldCount = sceneArchetype.layout->count;

    positions         = (Vec3 *)fields[0];
    rotations         = (Vec4 *)fields[1];
    scales            = (Vec3 *)fields[2];
    isActive          = (b8 *)fields[3];
    names             = (c8 *)fields[4];
    modelIDs          = (u32 *)fields[5];
    shaderHandles     = (u32 *)fields[6];
    entityMaterialIDs = (u32 *)fields[7];
    archetypeIDs      = (fieldCount > 8)  ? (u32 *)fields[8]  : nullptr;
    archetypeHashes   = (fieldCount > 9)  ? (u64 *)fields[9]  : nullptr;
    sceneCameraFlags  = (fieldCount > 10) ? (b8 *)fields[10]  : nullptr;
    ecsSlotIDs        = (fieldCount > 11) ? (u32 *)fields[11] : nullptr;
    entityTags        = (fieldCount > 12) ? (c8 *)fields[12]  : nullptr;
    physicsBodyTypes  = (fieldCount > 13) ? (u32 *)fields[13] : nullptr;
    masses            = (fieldCount > 14) ? (f32 *)fields[14] : nullptr;
    colliderShapes    = (fieldCount > 15) ? (u32 *)fields[15] : nullptr;
    sphereRadii       = (fieldCount > 16) ? (f32 *)fields[16] : nullptr;
    colliderHalfXs    = (fieldCount > 17) ? (f32 *)fields[17] : nullptr;
    colliderHalfYs    = (fieldCount > 18) ? (f32 *)fields[18] : nullptr;
    colliderHalfZs    = (fieldCount > 19) ? (f32 *)fields[19] : nullptr;
    isLight           = (fieldCount > 20) ? (b8 *)fields[20]  : nullptr;
    lightTypes        = (fieldCount > 21) ? (u32 *)fields[21] : nullptr;
    lightRanges       = (fieldCount > 22) ? (f32 *)fields[22] : nullptr;
    lightColorRs      = (fieldCount > 23) ? (f32 *)fields[23] : nullptr;
    lightColorGs      = (fieldCount > 24) ? (f32 *)fields[24] : nullptr;
    lightColorBs      = (fieldCount > 25) ? (f32 *)fields[25] : nullptr;
    lightIntensities  = (fieldCount > 26) ? (f32 *)fields[26] : nullptr;
    lightDirXs        = (fieldCount > 27) ? (f32 *)fields[27] : nullptr;
    lightDirYs        = (fieldCount > 28) ? (f32 *)fields[28] : nullptr;
    lightDirZs        = (fieldCount > 29) ? (f32 *)fields[29] : nullptr;
    lightInnerCones   = (fieldCount > 30) ? (f32 *)fields[30] : nullptr;
    lightOuterCones   = (fieldCount > 31) ? (f32 *)fields[31] : nullptr;
    colliderOffsetXs   = (fieldCount > 32) ? (f32 *)fields[32] : nullptr;
    colliderOffsetYs   = (fieldCount > 33) ? (f32 *)fields[33] : nullptr;
    colliderOffsetZs   = (fieldCount > 34) ? (f32 *)fields[34] : nullptr;
}

// After loading an old scene whose layout has fewer fields than the current
// SceneEntity definition, migrate the data into a fresh archetype with the
// full layout. Missing fields (e.g. archetypeID) are zero-filled.
void migrateSceneArchetypeIfNeeded()
{
    u32 loadedFields = sceneArchetype.layout->count;
    u32 currentFields = SceneEntity.count;
    if (loadedFields >= currentFields)
        return; // already up-to-date

    // Validate arena exists before accessing
    if (!sceneArchetype.arena || sceneArchetype.arenaCount == 0)
    {
        ERROR("migrateSceneArchetypeIfNeeded: archetype has no valid arena");
        return;
    }

    u32 liveCount = sceneArchetype.arena[0].count;
    u32 capacity  = sceneArchetype.capacity;

    // Save old field pointers, sizes, and names (names are freed with the layout below)
    void **oldFields = getArchetypeFields(&sceneArchetype, 0);
    StructLayout *oldLayout = sceneArchetype.layout;

    void  *fieldCopies[32] = {0};
    u32    fieldSizes[32]  = {0};
    c8     fieldNames[32][64] = {0};
    for (u32 i = 0; i < loadedFields && i < 32; i++)
    {
        u32 bytes = oldLayout->fields[i].size * liveCount;
        fieldSizes[i] = oldLayout->fields[i].size;
        if (oldLayout->fields[i].name)
            strncpy(fieldNames[i], oldLayout->fields[i].name, 63);
        if (bytes > 0)
        {
            fieldCopies[i] = malloc(bytes);
            memcpy(fieldCopies[i], oldFields[i], bytes);
        }
    }

    // Destroy old archetype (frees its arenas and dynamic layout from loadScene)
    // The layout was dynamically allocated by loadScene, so destroyArchetype frees
    // arenas but not the layout itself. We free it manually.
    StructLayout *dynLayout = sceneArchetype.layout;
    destroyArchetype(&sceneArchetype);
    // Free the dynamic layout fields and name allocated by loadScene
    if (dynLayout != &SceneEntity)
    {
        if (dynLayout->fields)
        {
            for (u32 i = 0; i < loadedFields; i++)
            {
                if (dynLayout->fields[i].name)
                {
                    u32 len = (u32)strlen(dynLayout->fields[i].name) + 1;
                    dfree((void *)dynLayout->fields[i].name, len, MEM_TAG_SCENE);
                }
            }
            dfree(dynLayout->fields, sizeof(FieldInfo) * loadedFields, MEM_TAG_SCENE);
        }
        if (dynLayout->name)
        {
            u32 len = (u32)strlen(dynLayout->name) + 1;
            dfree((void *)dynLayout->name, len, MEM_TAG_SCENE);
        }
        dfree(dynLayout, sizeof(StructLayout), MEM_TAG_SCENE);
    }

    // Create new archetype with current SceneEntity layout
    if (!createArchetype(&SceneEntity, capacity, &sceneArchetype))
    {
        FATAL("migrateSceneArchetypeIfNeeded: failed to create migrated archetype");
        return;
    }

    // Copy old data into new archetype fields by name so inserting fields in the
    // middle of the layout doesn't silently corrupt data (index-based copy breaks
    // whenever a field is inserted before the end).
    void **newFields = getArchetypeFields(&sceneArchetype, 0);
    for (u32 oldI = 0; oldI < loadedFields; oldI++)
    {
        if (!fieldCopies[oldI] || liveCount == 0) continue;
        for (u32 newI = 0; newI < currentFields; newI++)
        {
            if (strcmp(fieldNames[oldI], SceneEntity.fields[newI].name) == 0 &&
                fieldSizes[oldI] == SceneEntity.fields[newI].size)
            {
                memcpy(newFields[newI], fieldCopies[oldI], fieldSizes[oldI] * liveCount);
                break;
            }
        }
    }

    // For archetypeID: if it's genuinely new (old schema didn't have it), default to -1.
    // All other new fields are left at zero (createArchetype zero-initialises arenas).
    if (loadedFields <= 8)
    {
        u32 *ids = (u32 *)newFields[8];
        for (u32 e = 0; e < liveCount; e++)
            ids[e] = (u32)-1;
    }

    // Restore live count
    sceneArchetype.arena[0].count = liveCount;

    // Free temp copies
    for (u32 i = 0; i < loadedFields && i < 32; i++)
    {
        if (fieldCopies[i])
            free(fieldCopies[i]);
    }

    INFO("Migrated scene archetype from %u to %u fields (%u entities)",
         loadedFields, currentFields, liveCount);
}

b8 *isActive = nullptr;
Vec3 *positions = nullptr;
Vec4 *rotations = nullptr;
Vec3 *scales = nullptr;
c8 *names = nullptr;
u32 *modelIDs = nullptr;
u32 *shaderHandles = nullptr;
u32 *entityMaterialIDs = nullptr;
u32 *archetypeIDs = nullptr;
u64 *archetypeHashes = nullptr;
u32 *ecsSlotIDs = nullptr;
c8  *entityTags = nullptr;
u32 *physicsBodyTypes = nullptr;
f32 *masses = nullptr;
u32 *colliderShapes = nullptr;
f32 *sphereRadii = nullptr;
f32 *colliderHalfXs = nullptr;
f32 *colliderHalfYs = nullptr;
f32 *colliderHalfZs = nullptr;
f32 *colliderOffsetXs = nullptr;
f32 *colliderOffsetYs = nullptr;
f32 *colliderOffsetZs = nullptr;
b8  *isLight = nullptr;
u32 *lightTypes = nullptr;
f32 *lightRanges = nullptr;
f32 *lightColorRs = nullptr;
f32 *lightColorGs = nullptr;
f32 *lightColorBs = nullptr;
f32 *lightIntensities = nullptr;
f32 *lightDirXs = nullptr;
f32 *lightDirYs = nullptr;
f32 *lightDirZs = nullptr;
f32 *lightInnerCones = nullptr;
f32 *lightOuterCones = nullptr;
Material *materials = nullptr;

void init()
{
    entitySize = entityDefaultCount;
    entitySizeCache = entitySize;
    u32 outArenas = 0;
    if (!createArchetype(&SceneEntity, entitySizeCache, &sceneArchetype))
    {
        FATAL("Failed to create archetype for scene entities");
        return;
    }

    DEBUG("Archetype created with capacity: %d\n", entitySizeCache);

    rebindArchetypeFields();
    DEBUG("Setting up ImGui with SDL");
    // initializes imgui, resources and default scene
    //  After SDL window and OpenGL context creation:
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    DEBUG("Created Context");
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    // Persist editor layout to an absolute path next to the executable
    // so the .ini is found regardless of working directory.
    static char iniPath[512] = "";
    if (iniPath[0] == '\0')
    {
        const char *base = SDL_GetBasePath();
        if (base)
            snprintf(iniPath, sizeof(iniPath), "%simgui_editor.ini", base);
        else
            snprintf(iniPath, sizeof(iniPath), "imgui_editor.ini");
    }
    io.IniFilename = iniPath;

    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    // Initialize ImGui backends
    ImGui_ImplSDL3_InitForOpenGL(display->sdlWindow,
                                 display->glContext);
    ImGui_ImplOpenGL3_Init("#version 410");

    // compile simple lighting shader that exists in the testbed resources
    // folder
    shader = createGraphicsProgram("../res/shader.vert", "../res/shader.frag");
    materialUniforms = getMaterialUniforms(shader);

    arrowShader =
        createGraphicsProgram("../res/arrow.vert", "../res/arrow.frag");

    colourLocation = glGetUniformLocation(arrowShader, "colour");

    initCamera(&sceneCam, {0.0f, 0.0f, 5.0f}, // position
               70.0f,                         // field of view
               1.0f,          // aspect (real aspect fixed every frame)
               0.1f, 100.0f); // near/far clip planes

    cubeMapTexture = 0;
    skyboxMesh = createSkyboxMesh(); // generate cube mesh (36 verts)
    skyboxShader =
        createGraphicsProgram("../res/Skybox.vert", "../res/Skybox.frag");
    loadPreferredSkybox();

    // Enable seamless cubemap sampling to reduce visible seams when sampling
    // across cube faces (requires OpenGL 3.2+). This tells the GL to sample
    // consistently across edges instead of using per-face coordinates.
#ifdef GL_TEXTURE_CUBE_MAP_SEAMLESS
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#endif

    // cache uniform locations for efficiency
    skyboxViewLoc = glGetUniformLocation(skyboxShader, "view");
    skyboxProjLoc = glGetUniformLocation(skyboxShader, "projection");

    // Load FBO post-processing shader
    fboShader =
        createGraphicsProgram("../res/FBOShader.vert", "../res/FBOShader.frag");
    if (fboShader == 0)
    {
        WARN("Failed to load FBO shader - post-processing effects disabled");
    }

    // Load deferred lighting shader
    deferredLightingShader =
        createGraphicsProgram("../res/deferred_lighting.vert", "../res/deferred_lighting.frag");
    if (deferredLightingShader == 0)
    {
        WARN("Failed to load deferred lighting shader — deferred rendering disabled");
    }

    // ---- Create the global Renderer so ECS systems can acquire GBuffers/InstanceBuffers ----
    if (!renderer)
    {
        Renderer *r = createRenderer(display,
                                     70.0f,  // fov
                                     0.1f,   // near clip
                                     100.0f, // far clip
                                     8, 16, 8); // max cameras, instance buffers, gbuffers
        if (r)
        {
            r->defaultShader = shader;

            // Acquire a camera in the renderer and sync it with sceneCam
            g_editorCamSlot = rendererAcquireCamera(r, sceneCam.pos, 70.0f, 1.0f, 0.1f, 100.0f);
            rendererSetActiveCamera(r, g_editorCamSlot);
            r->envMapTex = cubeMapTexture;
            INFO("Editor: created Renderer (defaultShader=%u, cam=%u)", shader, g_editorCamSlot);
        }
        else
        {
            WARN("Editor: failed to create Renderer — ECS rendering disabled");
        }
    }

    // Initialize the immediate-mode gizmo drawing system (GL_LINES overlay)
    gizmoInit();

    cubeMesh = createBoxMesh();

    // register built-in primitive models (box, plane, sphere)
    {
        Mesh *boxPrim   = createBoxMesh();
        Mesh *planePrim = createPlaneMesh();
        Mesh *spherePrim = createSphereMesh();
        if (boxPrim)    resRegisterPrimitive(resources, "Box",    boxPrim);
        if (planePrim)  resRegisterPrimitive(resources, "Plane",  planePrim);
        if (spherePrim) resRegisterPrimitive(resources, "Sphere", spherePrim);
        dfree(boxPrim, sizeof(Mesh), MEM_TAG_MESH);
        dfree(planePrim, sizeof(Mesh), MEM_TAG_MESH);
        dfree(spherePrim, sizeof(Mesh), MEM_TAG_MESH);
    }

    // initialize ID framebuffer for entity picking
    initIDFramebuffer();

    initMultiFBOs();

    DEBUG("Resource manager has %d models and %d meshes", resources->modelUsed,
          resources->meshUsed);

    // --- Auto-load first scene file on startup ---
    {
        c8 scenesDir[512];
        snprintf(scenesDir, sizeof(scenesDir), "%s/scenes", hubProjectDir);

        u32 totalFiles = 0;
        c8 **allFiles = listFilesInDirectory(scenesDir, &totalFiles);

        const c8 *firstScene = nullptr;
        if (allFiles && totalFiles > 0)
        {
            for (u32 i = 0; i < totalFiles; i++)
            {
                u32 len = (u32)strlen(allFiles[i]);
                if (len > 5 && strcmp(allFiles[i] + len - 5, ".drsc") == 0)
                {
                    if (!firstScene)
                        firstScene = allFiles[i];
                }
            }
        }

        if (firstScene)
        {
            INFO("Auto-loading scene: %s", firstScene);
            SceneData sd = loadScene(firstScene);
            sceneRemapModelIDs(&sd);
            //copy the first scene's archetype data into our scene path buffer
            strncpy(scenePathBuffer, firstScene, sizeof(scenePathBuffer));
            scenePathBuffer[sizeof(scenePathBuffer) - 1] = '\0';

            if (sd.archetypeCount > 0 && sd.archetypes)
            {
                destroyArchetype(&sceneArchetype);

                sceneArchetype  = sd.archetypes[0];
                entitySizeCache = sceneArchetype.capacity;
                
                // Validate that arena was properly initialized before accessing it
                if (sceneArchetype.arenaCount > 0 && sceneArchetype.arena)
                {
                    entityCount = sceneArchetype.arena[0].count;
                }
                else
                {
                    ERROR("Auto-load: loaded archetype has no valid arena (arenaCount=%u, arena=%p)",
                          sceneArchetype.arenaCount, sceneArchetype.arena);
                    entityCount = 0;
                }
                entitySize      = (i32)entitySizeCache;

                // Migrate old scenes that lack newer fields (e.g. archetypeID)
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

                if (g_startupModelRefs)
                {
                    dfree(g_startupModelRefs, sizeof(c8[MAX_NAME_SIZE]) * g_startupModelRefCount, MEM_TAG_SCENE);
                    g_startupModelRefs = nullptr;
                    g_startupModelRefCount = 0;
                }
                g_startupModelRefs = sd.modelRefs;
                g_startupModelRefCount = sd.modelRefCount;
                sd.modelRefs = nullptr;
                sd.modelRefCount = 0;

                INFO("Loaded startup scene (%u entities, %u materials)",
                     entityCount, sd.materialCount);
            }
        }

        // Free file list
        if (allFiles)
        {
            for (u32 i = 0; i < totalFiles; i++)
                free(allFiles[i]);
            free(allFiles);
        }
    }

    // Scan the project's src/ for existing archetype system files so the
    // archetype designer shows them immediately on project open.
    scanProjectArchetypes(hubProjectDir);

    // Load the project's own res/ folder into the resource manager so that
    // project models, textures and shaders are available in the editor.
    if (hubProjectDir[0] != '\0')
    {
        c8 projRes[MAX_PATH_LENGTH];
        snprintf(projRes, sizeof(projRes), "%s/res/", hubProjectDir);
        if (fileExists(projRes) || true)  // readResources handles missing dir gracefully
            readResources(resources, projRes);
        c8 projTexMetaPath[MAX_PATH_LENGTH];
        snprintf(projTexMetaPath, sizeof(projTexMetaPath), "%stextures.meta", projRes);
        loadTextureMetadata(projTexMetaPath);
        applyMaterialPresets(resources, hubProjectDir);
        buildShaderSourceTable(projRes);

        if (g_startupModelRefs && g_startupModelRefCount > 0)
        {
            SceneData remapData = {0};
            remapData.archetypeCount = 1;
            remapData.archetypes = &sceneArchetype;
            remapData.modelRefCount = g_startupModelRefCount;
            remapData.modelRefs = g_startupModelRefs;

            u32 remapped = sceneRemapModelIDs(&remapData);
            if (remapped > 0)
                INFO("Startup scene model IDs remapped: %u", remapped);

            dfree(g_startupModelRefs, sizeof(c8[MAX_NAME_SIZE]) * g_startupModelRefCount, MEM_TAG_SCENE);
            g_startupModelRefs = nullptr;
            g_startupModelRefCount = 0;
        }
    }

    //set vsync off
    SDL_GL_SetSwapInterval(0);
}

Vec2 cacheMouse = {0, 0};
PickResult result;

b8 canSave = true;
static b8 canBuildRun = true;
static b8 canStop = true;
void update(f32 dt)
{
    if (entitySize != entitySizeCache)
    {
        DEBUG("changed size %d", entitySize);
        // this means we need to re allocate the Entity EntityArena
        reallocateSceneArena();
        // set the cache
        entitySizeCache = entitySize;
    }

    // Gizmo drag/release must run regardless of viewport hover so that dragging
    // the mouse outside the viewport doesn't cancel the operation mid-drag.
    ImVec2 mousePos = ImGui::GetMousePos();
    Vec2 mPos = {mousePos.x, mousePos.y};

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        canMoveAxis = false;

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && canMoveAxis
        && inspectorEntityID < entitySizeCache)
    {
        Vec2 delta = v2Sub(mPos, cacheMouse);
        cacheMouse = mPos;

        // Project the world axis into screen space so drag direction is
        // correct regardless of camera angle.
        f32 mag = 0.0f;
        {
            Mat4 view      = getView(&sceneCam, false);
            Mat4 vp        = mat4Mul(sceneCam.projection, view);
            Vec3 entityPos = positions[inspectorEntityID];
            Vec4 wp0       = {entityPos.x, entityPos.y, entityPos.z, 1.0f};
            Vec4 p0        = mat4TransformVec4(vp, wp0);
            Vec3 ep1       = v3Add(entityPos, manipulateAxis);
            Vec4 wp1       = {ep1.x, ep1.y, ep1.z, 1.0f};
            Vec4 p1        = mat4TransformVec4(vp, wp1);
            if (p0.w > 0.0001f && p1.w > 0.0001f)
            {
                Vec2 screenAxis = {
                    (p1.x / p1.w - p0.x / p0.w) * (f32)viewportWidth  * 0.5f,
                    -(p1.y / p1.w - p0.y / p0.w) * (f32)viewportHeight * 0.5f
                };
                f32 axisLen = v2Mag(screenAxis);
                if (axisLen > 0.001f)
                    mag = v2Dot(delta, v2Scale(screenAxis, 1.0f / axisLen));
            }
        }

        switch (manipulateState)
        {
        case MANIPULATE_POSITION:
            positions[inspectorEntityID] = v3Add(positions[inspectorEntityID],
                                                 v3Scale(manipulateAxis, mag * 0.01f));
            break;
        case MANIPULATE_ROTATION:
        {
            Vec4 dq = quatFromAxisAngle(manipulateAxis, mag * 0.01f);
            rotations[inspectorEntityID] = quatMul(dq, rotations[inspectorEntityID]);
            break;
        }
        case MANIPULATE_SCALE:
            scales[inspectorEntityID] = v3Add(scales[inspectorEntityID],
                                              v3Scale(manipulateAxis, mag * 0.01f));
            break;
        default: break;
        }
    }

    if (canMoveViewPort && !g_gameRunning)
    {
        moveViewPortCamera(dt);

        if (isKeyDown(KEY_E))      manipulateState = MANIPULATE_POSITION;
        else if (isKeyDown(KEY_R)) manipulateState = MANIPULATE_ROTATION;
        else if (isKeyDown(KEY_T)) manipulateState = MANIPULATE_SCALE;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            result = getEntityAtMouse(mousePos, g_viewportScreenPos);

            if (result.type == PICK_ENTITY)
            {
                u32 id = result.entityID;
                if (id > 0 && id <= entitySizeCache)
                {
                    u32 selectedEntity = id - 1;
                    inspectorEntityID = selectedEntity;
                    currentInspectorState = ENTITY_VIEW;
                    manipulateTransform = true;
                }
                else
                {
                    manipulateTransform = false;
                }
            }
            else if (result.type == PICK_GIZMO_X || result.type == PICK_GIZMO_Y ||
                     result.type == PICK_GIZMO_Z)
            {
                canMoveAxis = true;
                cacheMouse  = mPos;
                switch (result.type)
                {
                case PICK_GIZMO_X: manipulateAxis = v3Right;   break;
                case PICK_GIZMO_Y: manipulateAxis = v3Up;      break;
                case PICK_GIZMO_Z: manipulateAxis = v3Forward; break;
                default:           manipulateAxis = v3Zero;    break;
                }
            }
            else if (result.type == PICK_NONE)
            {
                manipulateTransform = false;
                canMoveAxis = false;
            }
        }
    }

    //if cntrl + s is pressed then save the scene
    // add a canSave flag to prevent multiple saves on key hold

    // F5 = Build & Run,  Shift+F5 = Stop
    const b8 f5Down = isKeyDown(KEY_F5);
    const b8 shiftDown = isKeyDown(KEY_LSHIFT) || isKeyDown(KEY_RSHIFT);

    if (canBuildRun && f5Down && !shiftDown)
    {
        canBuildRun = false;
        doBuildAndRun();
    }
    else if (!canBuildRun && !f5Down)
    {
        canBuildRun = true;
    }

    if (canStop && f5Down && shiftDown)
    {
        canStop = false;
        doStopGame();
    }
    else if (!canStop && !f5Down)
    {
        canStop = true;
    }

    const b8 ctrlSaveDown = isKeyDown(KEY_LCTRL) && isKeyDown(KEY_S);
    if (canSave && ctrlSaveDown)
    {
        canSave = false; // prevent multiple saves on key hold
        SceneData sd = {0};
        sd.archetypeCount = 1;
        sd.archetypes = &sceneArchetype;
        sd.materialCount = resources->materialUsed;
        sd.materials = resources->materialBuffer;
        syncSceneArchetypeHashesFromIDs();

        //copy the archetype name into the scene data
        strncpy(sd.archetypeNames[0], "SceneEntity", MAX_SCENE_NAME - 1);
        DEBUG("Saving to path: %s", scenePathBuffer);
        if (saveScene(scenePathBuffer, &sd))
        {
            INFO("Scene saved successfully to %s", scenePathBuffer);
        }
        else
        {
            ERROR("Failed to save scene to %s", scenePathBuffer);
        }

    }
    else if (!canSave && !ctrlSaveDown)
    {
        canSave = true; // reset flag when keys are released
    }

    // Ctrl+C / Ctrl+V entity copy-paste (edit mode only, not while typing in ImGui)
    {
        static u8  clipboardBuf[4096];
        static u32 clipboardSize  = 0;
        static b8  clipboardValid = false;
        static b8  canCopy        = true;
        static b8  canPaste       = true;

        const ImGuiIO &io        = ImGui::GetIO();
        // Only block when the user is actively typing into a text field —
        // WantCaptureKeyboard is true for any focused ImGui window, which would
        // swallow the shortcut after clicking an entity in the Scene List.
        const b8 typingInImGui   = io.WantTextInput;
        const b8 ctrlDown        = isKeyDown(KEY_LCTRL) || isKeyDown(KEY_RCTRL);
        const b8 copyDown        = ctrlDown && isKeyDown(KEY_C);
        const b8 pasteDown       = ctrlDown && isKeyDown(KEY_V);
        const b8 inspectingEntity = (currentInspectorState == ENTITY_VIEW)
                                     && (inspectorEntityID < entityCount);

        if (canCopy && copyDown && !typingInImGui && !g_gameRunning && inspectingEntity)
        {
            canCopy = false;
            void **fields = getArchetypeFields(&sceneArchetype, 0);
            StructLayout *layout = sceneArchetype.layout;
            if (fields && layout)
            {
                u32 off = 0;
                for (u32 f = 0; f < layout->count; f++)
                {
                    u32 sz = layout->fields[f].size;
                    if (off + sz > sizeof(clipboardBuf)) { off = 0; break; }
                    memcpy(clipboardBuf + off,
                           (u8 *)fields[f] + (u64)sz * inspectorEntityID, sz);
                    off += sz;
                }
                clipboardSize  = off;
                clipboardValid = (off > 0);
                if (clipboardValid)
                    INFO("Copied entity %u", inspectorEntityID);
            }
        }
        else if (!canCopy && !copyDown)
        {
            canCopy = true;
        }

        if (canPaste && pasteDown && !typingInImGui && !g_gameRunning && clipboardValid)
        {
            canPaste = false;
            if (entityCount >= entitySizeCache)
            {
                WARN("Paste failed: entity cap reached (%u)", entitySizeCache);
            }
            else
            {
                void **fields = getArchetypeFields(&sceneArchetype, 0);
                StructLayout *layout = sceneArchetype.layout;
                if (fields && layout)
                {
                    u32 dst = entityCount;
                    u32 off = 0;
                    for (u32 f = 0; f < layout->count; f++)
                    {
                        u32 sz = layout->fields[f].size;
                        if (off + sz > clipboardSize) break;
                        memcpy((u8 *)fields[f] + (u64)sz * dst,
                               clipboardBuf + off, sz);
                        off += sz;
                    }

                    // Nudge position so the clone is visible, and rename it.
                    if (positions) positions[dst].x += 1.0f;
                    if (names)
                    {
                        c8 *n = &names[dst * MAX_NAME_SIZE];
                        u32 nl = (u32)strlen(n);
                        const c8 *suffix = " (copy)";
                        u32 sl = (u32)strlen(suffix);
                        if (nl + sl < MAX_NAME_SIZE) memcpy(n + nl, suffix, sl + 1);
                    }
                    // A pasted entity is a *new* ECS slot — clear the runtime id
                    // so it gets a fresh mapping on next play.
                    if (ecsSlotIDs) ecsSlotIDs[dst] = (u32)-1;

                    entityCount++;
                    sceneArchetype.arena[0].count = entityCount;
                    if (sceneArchetype.activeChunkCount == 0)
                        sceneArchetype.activeChunkCount = 1;
                    inspectorEntityID = dst;
                    currentInspectorState = ENTITY_VIEW;
                    INFO("Pasted entity -> slot %u", dst);
                }
            }
        }
        else if (!canPaste && !pasteDown)
        {
            canPaste = true;
        }
    }
}

static void saveCurrentScene()
{
    if (scenePathBuffer[0] == '\0') return;
    SceneData sd = {0};
    sd.archetypeCount = 1;
    sd.archetypes     = &sceneArchetype;
    sd.materialCount  = resources->materialUsed;
    sd.materials      = resources->materialBuffer;
    syncSceneArchetypeHashesFromIDs();
    strncpy(sd.archetypeNames[0], "SceneEntity", MAX_SCENE_NAME - 1);
    if (saveScene(scenePathBuffer, &sd))
        INFO("Scene saved to %s", scenePathBuffer);
    else
        ERROR("Failed to save scene to %s", scenePathBuffer);
}

void render(f32 dt)
{
    // begin new imgui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    drawDockspaceAndPanels();

    // Quit confirmation modal — opened when the window close button is pressed
    if (g_quitRequested)
        ImGui::OpenPopup("Save before exiting?");

    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save before exiting?", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoMove))
    {
        ImGui::Text("You have unsaved changes.");
        ImGui::Text("Do you want to save the scene before exiting?");
        ImGui::Spacing();

        if (ImGui::Button("Save & Exit", ImVec2(120, 0)))
        {
            saveCurrentScene();
            editor->state = EXIT;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Exit without Saving", ImVec2(160, 0)))
        {
            editor->state = EXIT;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
        {
            g_quitRequested = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // multi-viewport support
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

void destroy()
{
    if (hubProjectDir[0] != '\0')
    {
        c8 projTexMetaPath[MAX_PATH_LENGTH];
        snprintf(projTexMetaPath, sizeof(projTexMetaPath), "%s/res/textures.meta", hubProjectDir);
        saveTextureMetadata(projTexMetaPath);
    }

    if (g_gameRunning && g_gameDLL.loaded)
    {
        g_gameDLL.plugin.destroy();
        unloadGameDLL(&g_gameDLL);
        g_gameRunning = false;
    }

    if (g_startupModelRefs)
    {
        dfree(g_startupModelRefs, sizeof(c8[MAX_NAME_SIZE]) * g_startupModelRefCount, MEM_TAG_SCENE);
        g_startupModelRefs = nullptr;
        g_startupModelRefCount = 0;
    }

    // destroy archetype and free its arenas
    destroyArchetype(&sceneArchetype);
    // clear pointers to archetype fields
    positions = NULL;
    rotations = NULL;
    scales = NULL;
    isActive = NULL;
    names = NULL;
    modelIDs = NULL;
    shaderHandles = NULL;
    entityMaterialIDs = NULL;
    shutdownMaterialRegistry();
    freeMesh(cubeMesh);
    freeShader(shader);
    freeShader(arrowShader);
    freeShader(fboShader);
    freeMesh(skyboxMesh);
    freeTexture(cubeMapTexture);
    freeShader(skyboxShader);
    destroyIDFramebuffer();
    destroyMultiFBOs();

    // Shutdown physics and gizmo systems
    physShutdown();
    gizmoShutdown();

    // destroy the global renderer
    if (renderer)
        destroyRenderer(renderer);

    if (consoleLines)
    {
        for (u32 i = 0; i < MAX_CONSOLE_LINES; i++)
        {
            if (consoleLines[i]) free((void *)consoleLines[i]);
        }
        free(consoleLines);
        consoleLines = NULL;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

i32 main(i32 argc, char **argv)
{
    // Run the hub first so the user can select a project
    hubApplication = createApplication("Druid Hub", hubStart, hubUpdate, hubRender, hubDestroy);
    hubApplication->width = 720.0f;
    hubApplication->height = 480.0f;
    hubApplication->inputProcess = hubProcessInput;
    run(hubApplication);

    // Only launch the editor if the user confirmed a project
    if (!hubProjectSelected)
    {
        SDL_Quit();
        return 0;
    }

    logOutputSrc = &editorLog;
    useCustomOutputSrc = true;
    consoleLines = (const c8**)calloc(MAX_CONSOLE_LINES, sizeof(c8*));
    editor = createApplication("Druid Editor",init, update, render, destroy);

    SDL_DisplayID displayID = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *dm = displayID ? SDL_GetCurrentDisplayMode(displayID) : nullptr;
    if (dm && dm->w > 0 && dm->h > 0)
    {
        editor->width  = (f32)(dm->w * 0.8f);
        editor->height = (f32)(dm->h * 0.8f);
    }
    else
    {
        editor->width  = 1920.0f;
        editor->height = 1080.0f;
    }
    viewportWidth = (u32)editor->width;
    viewportHeight = (u32)editor->height;
    editor->inputProcess = processInput;
    run(editor);
    SDL_Quit();
    return 0;
}
