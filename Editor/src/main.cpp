#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_impl_opengl3.h"
#include "../deps/imgui/imgui_impl_sdl3.h"
#include "../deps/imgui/imgui_internal.h"
#include "editor.h"
#include "entitypicker.h"
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
char inputBoxBuffer[100]; // this will be in numbers
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
}

void reallocateSceneArena()
{
}

b8 *isActive = nullptr;
Vec3 *positions = nullptr;
Vec4 *rotations = nullptr;
Vec3 *scales = nullptr;
char *names = nullptr;
u32 *modelIDs = nullptr;
u32 *shaderHandles = nullptr;
u32 *entityMaterialIDs = nullptr;
Material *materials = nullptr;

void init()
{
    // consoleLines = (const char**)malloc(sizeof(char*) * MAX_CONSOLE_LINES);

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

    void **fields = getArchetypeFields(&sceneArchetype, 0);
    if (!fields)
    {
        FATAL("Failed to get archetype fields");
        return;
    }

    positions = (Vec3 *)fields[0];
    rotations = (Vec4 *)fields[1];
    scales = (Vec3 *)fields[2];
    isActive = (b8 *)fields[3];
    names = (char *)fields[4];
    modelIDs = (u32 *)fields[5];
    shaderHandles = (u32 *)fields[6];
    entityMaterialIDs = (u32 *)fields[7];
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
    const char *faces[6] = {
        "../res/Textures/Skybox/right.jpg", "../res/Textures/Skybox/left.jpg",
        "../res/Textures/Skybox/top.jpg",   "../res/Textures/Skybox/bottom.jpg",
        "../res/Textures/Skybox/front.jpg", "../res/Textures/Skybox/back.jpg"};

    cubeMapTexture = createCubeMapTexture(faces, 6); // load cubemap from disk
    skyboxMesh = createSkyboxMesh(); // generate cube mesh (36 verts)
    skyboxShader =
        createGraphicsProgram("../res/Skybox.vert", "../res/Skybox.frag");

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

    // create a small cube mesh for gizmos and pickable handles
    cubeMesh = createBoxMesh();

    // initialize ID framebuffer for entity picking
    initIDFramebuffer();

    // initialize multi-FBO system
    initMultiFBOs();

    DEBUG("Resource manager has %d models and %d meshes", resources->modelUsed,
          resources->meshUsed);

    sceneManager = createSceneManager(DEFAULT_SCENE_CAPACITY);
    if (!sceneManager)
    {
        FATAL("Failed to create scene manager");
        return;
    }

    DEBUG("Scene manager created with capacity: %d", DEFAULT_SCENE_CAPACITY);

    // get all the .scene files in the scenes directory
    u32 out = 0;
    char **sceneFiles = listFilesInDirectory("../scenes/", &out);

    // check if there are any scene files
    if (out > 0)
    {
        SceneMetaData sceneData = {0};
        // TODO: Implement proper scene loading but for now just load the first
        // scene
        sceneData = loadScene(sceneFiles[0]);
        if (sceneData.entityCount > 0)
        {
            INFO("Loaded scene '%s' with %d entities", sceneFiles[0],
                 sceneData.entityCount);
            // add the loaded scene to the scene manager
            sceneManager->scenes[0] = sceneData;
            sceneManager->sceneCount = 1;

            // instantiate entities in the scene
            for (u32 i = 0; i < sceneData.entityCount; i++)
            {
            }
        }
    }
}

Vec2 cacheMouse = {0, 0};
PickResult result;
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

    if (canMoveViewPort)
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

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
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

            // determine direction of movement
            if (result.type == PICK_GIZMO_X)
            {
                mag = delta.x * speed;
            }
            else if (result.type == PICK_GIZMO_Y)
            {
                // flip the y
                mag = -(delta.y * speed);
            }
            else if (result.type == PICK_GIZMO_Z)
            {
                mag = -(delta.y * speed);
            }

            switch (manipulateState)
            {
            case MANIPULATE_POSITION:
            {
                // work out the transformation
                Vec3 transformation = v3Scale(v3Scale(manipulateAxis, mag), dt);

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
                Vec3 transformation = v3Scale(v3Scale(manipulateAxis, mag), dt);
                Vec3 *scale = &scales[inspectorEntityID];

                *scale = v3Add(*scale, transformation);

                break;
            }
            }
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && canMoveAxis &&
            manipulateTransform)
        {
            canMoveAxis = false;
            manipulateTransform = false;
            // DEBUG("Clicked off\n");
        }
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
    destroySceneManager(sceneManager);
    free(sceneManager);
    destroyMultiFBOs();

    ImGui_ImplOpenGL3_Shutdown(); // shutdown imgui opengl backend

    ImGui_ImplSDL3_Shutdown(); // shutdown imgui sdl backend
    ImGui::DestroyContext();   // destroy imgui core
}

int main(int argc, char **argv)
{

    // useCustomOutputSrc = true; //use custom output source for logging
    logOutputSrc = &editorLog;
    editor = createApplication(init, update, render, destroy);
    editor->width = (f32)(1920 * 1.25f);
    editor->height = (f32)(1080 * 1.25f);
    viewportWidth = editor->width;
    viewportHeight = editor->height;
    editor->inputProcess = processInput;
    run(editor);
    return 0;
}
