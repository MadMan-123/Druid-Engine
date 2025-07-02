// Bare-bones Editor main.cpp
#include <iostream>
#include <druid.h>
#include "..\deps\imgui\imgui.h"
#include "..\deps\imgui\imgui_impl_sdl3.h"
#include "..\deps\imgui\imgui_impl_opengl3.h"
#include "..\deps\imgui\imgui_internal.h"


Application* editor;
static SDL_Event evnt;
static u32 viewportFBO = 0;         // Framebuffer object for the Viewport panel
static u32 viewportTexture = 0;     // Texture attached to the FBO
static u32    viewportWidth  = 0;      // Current width  of the Viewport texture
static u32    viewportHeight = 0;      // Current height of the Viewport texture
static u32 depthRB = 0;

// ── Rendering resources ───────────────────────────────────────
static Mesh* cubeMesh   = nullptr;  //cube mesh
static u32   cubeShader = 0;        //cube shader

//skybox resources
static Mesh* skyboxMesh    = nullptr;  //unit cube mesh with inverted faces
static u32   cubeMapTexture = 0;       //cubemap texture handle
static u32   skyboxShader   = 0;       //skybox shader
static u32   skyboxViewLoc  = 0;       //uniform locations
static u32   skyboxProjLoc  = 0;       //uniform locations

static Camera    sceneCam;                // engine camera
static Transform cubeXform;               // model matrix data
static f32 rotationAngle = 0.0f;          //degrees – used to spin the cube
f32 yaw = 0;
f32 currentPitch = 0;
//constants for camera motion
static const f32 camMoveSpeed   = 1.0f;  //units per second
static const f32 camRotateSpeed = 5.0f;   //degrees per second

//helper – move camera with wasd keys
//move camera with wasd keys
static void moveCamera(f32 dt)
{
    if (isInputDown(KEY_W))
        moveForward(&sceneCam,  camMoveSpeed * dt);
    if (isInputDown(KEY_S))
        moveForward(&sceneCam, -camMoveSpeed * dt);
    if (isInputDown(KEY_A))
        moveRight(&sceneCam,  -camMoveSpeed * dt);
    if (isInputDown(KEY_D))
        moveRight(&sceneCam,   camMoveSpeed * dt);
}

void rotateCamera(f32 dt) 
{
	if (isMouseDown(SDL_BUTTON_RIGHT)) 
	{
		// Get the mouse delta
		f32 x, y;
		getMouseDelta(&x, &y);


		//apply the mouse delta to the camera
		yaw += -x * (camRotateSpeed) * dt;
		currentPitch += -y * (camRotateSpeed) * dt;


		//89 in radians
		f32 goal = radians(89.0f);
		// Clamp pitch to avoid gimbal lock
		currentPitch = clamp(currentPitch,-goal, goal);

		// Create yaw quaternion based on the world-up vector
		Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
		Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
		sceneCam.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
	}
}

// Helper that (re)creates the framebuffer and attached texture when the viewport size changes
//recreates viewport framebuffer when size changes
static void resizeViewportFramebuffer(int width, int height)
{
    if (width <= 0 || height <= 0) return;                   // Ignore invalid sizes
    if (width == viewportWidth && height == viewportHeight)  // No resize needed
        return;

    viewportWidth  = width;
    viewportHeight = height;

    if (viewportTexture == 0)
        glGenTextures(1, &viewportTexture);

    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportWidth, viewportHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (viewportFBO == 0)
        glGenFramebuffers(1, &viewportFBO);

    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportTexture, 0);

    if (depthRB == 0) glGenRenderbuffers(1, &depthRB);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRB);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewportWidth, viewportHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depthRB);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[Viewport] Framebuffer incomplete: 0x" << std::hex << status << std::dec << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void processInput(void* appData)
{
	//process sdl events and forward to imgui
	//void* should be Application
	Application* app = (Application*)appData;
    	//tell SDL to process events
	SDL_PumpEvents();

    	//get the current state of the keyboard
	while(SDL_PollEvent(&evnt)) //get and process events
	{
		//pass imgui events		
		ImGui_ImplSDL3_ProcessEvent(&evnt);
		switch (evnt.type)
		{
            //if the quit event is triggered then change the state to exit
			case SDL_EVENT_QUIT:
				app->state = EXIT;
				break;
			default: ;
		}
	}
	
}

void init()
{
	//initializes imgui, resources and default scene
	// After SDL window and OpenGL context creation:
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	ImGui::StyleColorsDark();

	// Initialize ImGui backends
	ImGui_ImplSDL3_InitForOpenGL(editor->display->sdlWindow, editor->display->glContext);
	ImGui_ImplOpenGL3_Init("#version 410"); // Or your GL version string

	editor->inputProcess = processInput;	

    //create demo cube mesh (defined in src/systems/Rendering/mesh.c)
    cubeMesh = createBoxMesh();

    //compile simple lighting shader that exists in the testbed resources folder
    cubeShader = createGraphicsProgram("../res/shader.vert",
                                       "../res/shader.frag");

    //setup camera that looks at the origin from z = +5
    initCamera(&sceneCam,
               (Vec3){0.0f, 0.0f, 5.0f},   //position
               70.0f,             //field of view
               1.0f,                       //aspect (real aspect fixed every frame)
               0.1f, 100.0f);              //near/far clip planes

    //initialize cube transform (identity)
    cubeXform.pos   = (Vec3){0,0,0};
    cubeXform.scale = (Vec3){0.1f,0.1f,0.1f};
    cubeXform.rot   = quatIdentity();

    //load skybox resources --------------------------------------------------
    const char* faces[6] = {
        "../res/Textures/Skybox/right.jpg",
        "../res/Textures/Skybox/left.jpg",
        "../res/Textures/Skybox/top.jpg",
        "../res/Textures/Skybox/bottom.jpg",
        "../res/Textures/Skybox/front.jpg",
        "../res/Textures/Skybox/back.jpg"
    };

    cubeMapTexture = createCubeMapTexture(faces, 6); //load cubemap from disk
    skyboxMesh     = createSkyboxMesh();             //generate cube mesh (36 verts)
    skyboxShader   = createGraphicsProgram("../res/Skybox.vert", "../res/Skybox.frag");

    //cache uniform locations for efficiency
    skyboxViewLoc  = glGetUniformLocation(skyboxShader, "view");
    skyboxProjLoc  = glGetUniformLocation(skyboxShader, "projection");
}

void update(f32 dt)
{
	//per-frame simulation update and camera motion
	//spin the cube so we have something moving
    rotationAngle += (45.0f)* dt;
    cubeXform.rot = quatFromAxisAngle(v3Up, radians(rotationAngle));

    //apply user input to the camera
    moveCamera(dt);
    rotateCamera(dt);
}

// Rendering helper functions to declutter render()
static void renderGameScene()
{
    //render the 3d scene to the off-screen framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
    glViewport(0, 0, viewportWidth, viewportHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //cube
    glUseProgram(cubeShader);
    updateShaderMVP(cubeShader, cubeXform, sceneCam);
    glUniform3f(glGetUniformLocation(cubeShader, "lightPos"), 1.0f, 2.0f, 1.0f);
    draw(cubeMesh);
    glUseProgram(0);

    //skybox
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(skyboxShader);

    Mat4 sbView = getView(&sceneCam, true);
    glUniformMatrix4fv(skyboxViewLoc, 1, GL_FALSE, &sbView.m[0][0]);
    glUniformMatrix4fv(skyboxProjLoc, 1, GL_FALSE, &sceneCam.projection.m[0][0]);

    glBindVertexArray(skyboxMesh->vao);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void drawViewportWindow()
{
    ImGui::Begin("Viewport");
    ImVec2 avail = ImGui::GetContentRegionAvail();

    const f32 targetAspect = 16.0f / 9.0f;
    f32 targetW = avail.x;
    f32 targetH = avail.x / targetAspect;
    if (targetH > avail.y)
    {
        targetH = avail.y;
        targetW = targetH * targetAspect;
    }

    resizeViewportFramebuffer((int)targetW, (int)targetH);

    ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - targetW) * 0.5f,
                               cursor.y + (avail.y - targetH) * 0.5f));

    //update camera projection for this aspect ratio
    sceneCam.projection = mat4Perspective(radians(70.0f), targetAspect, 0.1f, 100.0f);

    renderGameScene();

    ImGui::Image((void*)(intptr_t)viewportTexture, ImVec2(targetW, targetH), ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
}

static void drawDebugWindow()
{
    ImGui::Begin("Debug");
    ImGui::Text("FPS %lf", editor->fps);
    ImGui::End();
}

static void drawPrefabsWindow()
{
    ImGui::Begin("Prefabs");
    ImGui::Text("(Archetype designer coming soon)");
    ImGui::End();
}

static void drawSceneListWindow()
{
    ImGui::Begin("Scene List");
    ImGui::Text("(Scene hierarchy will appear here)");
    ImGui::End();
}

static void drawInspectorWindow()
{
    ImGui::Begin("Inspector");
    ImGui::Text("Component data here");
    ImGui::End();
}

static void drawDockspaceAndPanels()
{
    static bool dockspaceOpen = true;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.25f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", &dockspaceOpen, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceID");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    //build default layout once
    static bool first_time = true;
    if (first_time)
    {
        first_time = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

        ImGuiID dock_id_middle = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);
        ImGuiID dock_id_prefabs = ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Up, 0.5f, nullptr, &dock_id_middle);

        ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
        ImGui::DockBuilderDockWindow("Inspector", dock_id_right);
        ImGui::DockBuilderDockWindow("Scene List", dock_id_prefabs);
        ImGui::DockBuilderDockWindow("Prefabs", dock_id_middle);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    //--actual docked windows--
    drawViewportWindow();
    drawDebugWindow();
    drawPrefabsWindow();
    drawSceneListWindow();
    drawInspectorWindow();

    ImGui::End(); // MainDockSpace
}

void render(f32 dt)
{
    //begin new imgui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    drawDockspaceAndPanels();

    //render everything
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    //multi-viewport support
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

void destroy()
{
    //free editor resources before exiting
    freeMesh(cubeMesh);
    freeShader(cubeShader);

    //free skybox resources
    freeMesh(skyboxMesh);
    freeTexture(cubeMapTexture);
    freeShader(skyboxShader);

    ImGui_ImplOpenGL3_Shutdown(); //shutdown imgui opengl backend
    ImGui_ImplSDL3_Shutdown();    //shutdown imgui sdl backend
    ImGui::DestroyContext();      //destroy imgui core
}



int main(int argc, char** argv) 
{
	editor = createApplication(init, update, render, destroy);
	editor->width = 1920;
	editor->height = 1080;	
		
	run(editor); 
	return 0;
}
