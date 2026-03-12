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
Mesh *cubeMesh = nullptr;
u32 shader = 0;

f32 yaw = 0;
f32 currentPitch = 0;
static const f32 camMoveSpeed = 1.0f;   // units per second
static const f32 camRotateSpeed = 5.0f; // degrees per second
static const u32 entityDefaultCount = 128;
MaterialUniforms materialUniforms = {0};

Archetype sceneArchetype;
c8 inputBoxBuffer[100]; // this will be in numbers
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
    const f32 deadZone = 0.45f; // dead zone for input
    if (yInputAxis > deadZone)
    {
        moveForward(&sceneCam, camMoveSpeed * dt);
    }
    if (yInputAxis < -deadZone)
        moveForward(&sceneCam, -camMoveSpeed * dt);
    if (xInputAxis > deadZone)
        moveRight(&sceneCam, -camMoveSpeed * dt);
    if (xInputAxis < -deadZone)
        moveRight(&sceneCam, camMoveSpeed * dt);
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
    //  else if (v2Mag(axis) > 1.15f);
    //  {

    //      //apply the joystick input to the camera
    //      yaw += -axis.x * (camRotateSpeed) * dt;
    //      currentPitch += -axis.y * (camRotateSpeed) * dt;
    //      //89 in radians
    //      f32 goal = radians(89.0f);
    //      // Clamp pitch to avoid gimbal lock
    //      currentPitch = clamp(currentPitch,-goal, goal);
    //      // Create yaw quaternion based on the world-up vector
    //      Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
    //      Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
    // sceneCam.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
    //  }
}

void moveViewPortCamera(f32 dt)
{
    // apply user input to the camera
    moveCamera(dt);
    rotateCamera(dt);
}
void processInput(void *appData)
{
    // void* should be Application
    Application *app = (Application *)appData;
    // tell SDL to process events
    SDL_PumpEvents();

    // get the current state of the keyboard
    while (SDL_PollEvent(&evnt)) // get and process events
    {
        // pass imgui events
        ImGui_ImplSDL3_ProcessEvent(&evnt);
        switch (evnt.type)
        {
        // if the quit event is triggered then change the state to exit
        case SDL_EVENT_QUIT:
            app->state = EXIT;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            checkForGamepadConnection(&evnt);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            checkForGamepadRemoved(&evnt);
            break;
        default:;
        }
    }

    ImGuiIO &io = ImGui::GetIO();
    b8 capture = (b8)(io.WantCaptureMouse || io.WantCaptureKeyboard);

    // During play mode, let gameplay input through when the viewport is hovered.
    // This prevents UI panels from triggering gameplay actions, while allowing
    // controller/mouse input to drive the game in the viewport.
    if (g_gameRunning && canMoveViewPort)
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
    archetypeIDs      = (fieldCount > 8) ? (u32 *)fields[8] : nullptr;
    sceneCameraFlags  = (fieldCount > 9) ? (b8 *)fields[9] : nullptr;
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

    u32 liveCount = sceneArchetype.arena[0].count;
    u32 capacity  = sceneArchetype.capacity;

    // Save old field pointers and their sizes
    void **oldFields = getArchetypeFields(&sceneArchetype, 0);
    StructLayout *oldLayout = sceneArchetype.layout;

    // Allocate temp copies of old field data
    void  *fieldCopies[32] = {0};
    u32    fieldSizes[32]  = {0};
    for (u32 i = 0; i < loadedFields && i < 32; i++)
    {
        u32 bytes = oldLayout->fields[i].size * liveCount;
        fieldSizes[i] = oldLayout->fields[i].size;
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
                    free((void *)dynLayout->fields[i].name);
            }
            free(dynLayout->fields);
        }
        if (dynLayout->name)
            free((void *)dynLayout->name);
        free(dynLayout);
    }

    // Create new archetype with current SceneEntity layout
    if (!createArchetype(&SceneEntity, capacity, &sceneArchetype))
    {
        FATAL("migrateSceneArchetypeIfNeeded: failed to create migrated archetype");
        return;
    }

    // Copy old data into new archetype fields
    void **newFields = getArchetypeFields(&sceneArchetype, 0);
    for (u32 i = 0; i < loadedFields && i < currentFields; i++)
    {
        if (fieldCopies[i] && liveCount > 0)
        {
            // If old and new field sizes match, copy directly
            if (fieldSizes[i] == SceneEntity.fields[i].size)
            {
                memcpy(newFields[i], fieldCopies[i],
                       fieldSizes[i] * liveCount);
            }
        }
    }

    // Zero-fill new fields that didn't exist in the old layout
    for (u32 i = loadedFields; i < currentFields; i++)
    {
        memset(newFields[i], 0, SceneEntity.fields[i].size * capacity);
        // For archetypeID specifically, set all to (u32)-1 ("None")
        if (i == 8 && SceneEntity.fields[i].size == sizeof(u32))
        {
            u32 *ids = (u32 *)newFields[i];
            for (u32 e = 0; e < liveCount; e++)
                ids[e] = (u32)-1;
        }
        else if (i == 9 && SceneEntity.fields[i].size == sizeof(b8))
        {
            memset(newFields[i], 0, SceneEntity.fields[i].size * capacity);
        }
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
Material *materials = nullptr;

void init()
{
    entitySize = entityDefaultCount;
    entitySizeCache = entitySize;
    u32 outArenas = 0;
    // create the scene archetype (new archetype system)
    if (!createArchetype(&SceneEntity, entitySizeCache, &sceneArchetype))
    {
        FATAL("Failed to create archetype for scene entities");
        return;
    }

    DEBUG("Archetype created with capacity: %d\n", entitySizeCache);

    rebindArchetypeFields();
    // set to empty strings
    DEBUG("Setting up ImGui with SDL");
    // initializes imgui, resources and default scene
    //  After SDL window and OpenGL context creation:
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    DEBUG("Created Context");
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    // Initialize ImGui backends
    ImGui_ImplSDL3_InitForOpenGL(editor->display->sdlWindow,
                                 editor->display->glContext);
    ImGui_ImplOpenGL3_Init("#version 410");

    // compile simple lighting shader that exists in the testbed resources
    // folder
    shader = createGraphicsProgram("../res/shader.vert", "../res/shader.frag");
    materialUniforms = getMaterialUniforms(shader);

    arrowShader =
        createGraphicsProgram("../res/arrow.vert", "../res/arrow.frag");

    colourLocation = glGetUniformLocation(arrowShader, "colour");

    // setup camera that looks at the origin from z = +5
    initCamera(&sceneCam, {0.0f, 0.0f, 5.0f}, // position
               70.0f,                         // field of view
               1.0f,          // aspect (real aspect fixed every frame)
               0.1f, 100.0f); // near/far clip planes

    // load skybox resources --------------------------------------------------
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

    // ---- Create the global Renderer so ECS systems can acquire GBuffers/InstanceBuffers ----
    if (!renderer)
    {
        Renderer *r = createRenderer(editor->display,
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
            INFO("Editor: created Renderer (defaultShader=%u, cam=%u)", shader, g_editorCamSlot);
        }
        else
        {
            WARN("Editor: failed to create Renderer — ECS rendering disabled");
        }
    }

    // create a small cube mesh for gizmos and pickable handles
    cubeMesh = createBoxMesh();

    // initialize ID framebuffer for entity picking
    initIDFramebuffer();

    // initialize multi-FBO system
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
            //copy the first scene's archetype data into our scene path buffer
            strncpy(scenePathBuffer, firstScene, sizeof(scenePathBuffer));
            scenePathBuffer[sizeof(scenePathBuffer) - 1] = '\0';

            if (sd.archetypeCount > 0 && sd.archetypes)
            {
                destroyArchetype(&sceneArchetype);

                sceneArchetype  = sd.archetypes[0];
                entitySizeCache = sceneArchetype.capacity;
                entityCount     = sceneArchetype.arena[0].count;
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
                    free(sd.materials);
                }

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

    if (canMoveViewPort && !g_gameRunning)
    {
        moveViewPortCamera(dt);

        ImVec2 mousePos = ImGui::GetMousePos();
        Vec2 mPos = {mousePos.x, mousePos.y};

        if (isKeyDown(KEY_E))
        {
            manipulateState = MANIPULATE_POSITION;
        }
        else if (isKeyDown(KEY_R))
        {
            manipulateState = MANIPULATE_ROTATION;
        }
        else if (isKeyDown(KEY_T))
        {
            manipulateState = MANIPULATE_SCALE;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            // Debug print
            // DEBUG("Mouse Screen Pos: (%.2f, %.2f)\n", mousePos.x,
            // mousePos.y); DEBUG("Mouse Relative to Image: (%.2f, %.2f)\n",
            // relativeX, relativeY);

            result = getEntityAtMouse(mousePos, g_viewportScreenPos);

            if (result.type == PICK_ENTITY)
            {
                u32 id = result.entityID;
                if (id > 0 && id <= entitySizeCache)
                {
                    u32 selectedEntity = id - 1;
                    inspectorEntityID = selectedEntity;
                    currentInspectorState = ENTITY_VIEW;
                    INFO("Selected Entity %d: %s (Model ID: %d)\n",
                         selectedEntity, &names[selectedEntity * MAX_NAME_SIZE],
                         modelIDs[selectedEntity]);

                    // engage the transform manipulation tools
                    manipulateTransform = true;
                }
                else
                {
                    manipulateTransform = false;
                    // INFO("No entity selected (ID=%u)\n", id);
                }
            }

            if (result.type == PICK_GIZMO_X || result.type == PICK_GIZMO_Y ||
                result.type == PICK_GIZMO_Z)
            {
                canMoveAxis = true;
                cacheMouse = mPos; // seed so first drag delta is zero

                switch (result.type)
                {
                case PICK_GIZMO_X:
                    manipulateAxis = v3Right; // (1,0,0)
                    break;
                case PICK_GIZMO_Y:
                    manipulateAxis = v3Up; // (0,1,0)
                    break;
                case PICK_GIZMO_Z:
                    manipulateAxis = v3Forward; // (0,0,1) or v3Back if you're
                                                // using -Z forward
                    break;
                default:
                    manipulateAxis = v3Zero; // fallback
                    break;
                }
            }

            if (result.type == PICK_NONE)
            {
                manipulateTransform = false;
                canMoveAxis = false;
            }
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && canMoveAxis)
        {
            // Add bounds checking for entity access
            if (inspectorEntityID >= entitySizeCache)
            {
                WARN("Inspector entity ID %u out of bounds (max: %u)",
                     inspectorEntityID, entitySizeCache);
                return;
            }

            Vec2 delta = v2Sub(mPos, cacheMouse); // Correct delta direction
            cacheMouse = mPos;                    // Cache after delta is used

            f32 speed = 1.0f;
            f32 mag = 0.0f;

            // // determine direction of movement
            // if (result.type == PICK_GIZMO_X)
            // {
            //     mag = delta.x * speed;
            // }
            // else if (result.type == PICK_GIZMO_Y)
            // {
            //     // flip the y
            //     mag = -(delta.y * speed);
            // }
            // else if (result.type == PICK_GIZMO_Z)
            // {
            //     mag = -(delta.y * speed);
            // }


            // Project the manipulate axis into screen space to determine
            // how mouse movement maps to world-axis movement regardless
            // of camera angle.
            {
                Mat4 view = getView(&sceneCam, false);
                Mat4 vp   = mat4Mul(sceneCam.projection, view);

                Vec3 entityPos = positions[inspectorEntityID];
                // project entity position and entity position + axis into screen space
                Vec4 clipA = mat4TransformVec4(vp, {entityPos.x, entityPos.y, entityPos.z, 1.0f});
                Vec3 offsetPos = v3Add(entityPos, manipulateAxis);
                Vec4 clipB = mat4TransformVec4(vp, {offsetPos.x, offsetPos.y, offsetPos.z, 1.0f});

                // perspective divide to NDC
                Vec2 ndcA = {clipA.x / clipA.w, clipA.y / clipA.w};
                Vec2 ndcB = {clipB.x / clipB.w, clipB.y / clipB.w};

                // screen-space direction of the axis (in pixels)
                Vec2 screenAxis = {
                    (ndcB.x - ndcA.x) * viewportWidth  * 0.5f,
                    -(ndcB.y - ndcA.y) * viewportHeight * 0.5f   // flip Y (screen Y is down)
                };

                f32 axisLen = v2Mag(screenAxis);
                if (axisLen > 0.001f)
                {
                    // normalize the screen axis direction
                    screenAxis = v2Scale(screenAxis, 1.0f / axisLen);
                    // dot mouse delta with screen axis to get signed magnitude
                    mag = v2Dot(delta, screenAxis) * speed;
                }
            }

            switch (manipulateState)
            {
            case MANIPULATE_POSITION:
            {
                // work out the transformation
                Vec3 transformation = v3Scale(manipulateAxis, mag * 0.01f);

                Vec3 *pos = &positions[inspectorEntityID];
                // apply movement along axis
                *pos = v3Add(*pos, transformation);

                break;
            }
            case MANIPULATE_ROTATION:
            {
                // convert drag magnitude into radians
                f32 angle = mag * 0.01f;
                Vec4 *rotation = &rotations[inspectorEntityID];

                Vec4 deltaRotation = quatFromAxisAngle(manipulateAxis, angle);

                *rotation = quatMul(deltaRotation, *rotation);
                break;
            }
            case MANIPULATE_SCALE:
            {
                // edit the scale
                Vec3 transformation = v3Scale(manipulateAxis, mag * 0.01f);
                Vec3 *scale = &scales[inspectorEntityID];

                *scale = v3Add(*scale, transformation);

                break;
            }
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && canMoveAxis)
        {
            canMoveAxis = false;
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


}

void render(f32 dt)
{
    // begin new imgui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    drawDockspaceAndPanels();

    // render everything
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // multi-viewport support
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

void destroy()
{
    if (g_gameRunning && g_gameDLL.loaded)
    {
        g_gameDLL.plugin.destroy();
        unloadGameDLL(&g_gameDLL);
        g_gameRunning = false;
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
    // free editor resources before exiting
    freeMesh(cubeMesh);
    freeShader(shader);
    freeShader(arrowShader);
    freeShader(fboShader);
    // free skybox resources
    freeMesh(skyboxMesh);
    freeTexture(cubeMapTexture);
    freeShader(skyboxShader);
    destroyIDFramebuffer();
    destroyMultiFBOs();

    // destroy the global renderer
    if (renderer)
        destroyRenderer(renderer);

    // free console log lines
    if (consoleLines)
    {
        for (u32 i = 0; i < MAX_CONSOLE_LINES; i++)
        {
            if (consoleLines[i]) free((void *)consoleLines[i]);
        }
        free(consoleLines);
        consoleLines = NULL;
    }

    ImGui_ImplOpenGL3_Shutdown(); // shutdown imgui opengl backend

    ImGui_ImplSDL3_Shutdown(); // shutdown imgui sdl backend
    ImGui::DestroyContext();   // destroy imgui core
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
        return 0;

    logOutputSrc = &editorLog;
    useCustomOutputSrc = true;
    consoleLines = (const c8**)calloc(MAX_CONSOLE_LINES, sizeof(c8*));
    editor = createApplication("Druid Editor",init, update, render, destroy);
    editor->width = (f32)(1920 * 1.25f);
    editor->height = (f32)(1080 * 1.25f);
    viewportWidth = editor->width;
    viewportHeight = editor->height;
    editor->inputProcess = processInput;
    run(editor);
    return 0;
}
