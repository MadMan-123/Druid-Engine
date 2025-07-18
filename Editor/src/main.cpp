#include <iostream>
#include "druid.h"
#include "editor.h"
#include "scene.h"
#include "MeshMap.h"
#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_impl_sdl3.h"
#include "../deps/imgui/imgui_impl_opengl3.h"
#include "../deps/imgui/imgui_internal.h"


static SDL_Event evnt;
// ── Rendering resources ───────────────────────────────────────
Mesh* cubeMesh   = nullptr;  //cube mesh
u32   shader = 0;        //cube shader
               // engine camera
static Transform cubeXform;               // model matrix data
static f32 rotationAngle = 0.0f;          //degrees – used to spin the cube
f32 yaw = 0;
f32 currentPitch = 0;
//constants for camera motion
static const f32 camMoveSpeed   = 1.0f;  //units per second
static const f32 camRotateSpeed = 5.0f;   //degrees per second
static const u32 entityDefaultCount = 128;

EntityArena* sceneEntities;
char inputBoxBuffer[100]; //this will be in numbers 
i32 entitySize = 0;
//helper – move camera with wasd keys
bool canMoveViewPort = false;
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


void moveViewPortCamera(f32 dt)
{
     //apply user input to the camera
    moveCamera(dt);
    rotateCamera(dt);

}
void processInput(void* appData)
{
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

void reallocateSceneArena()
{

}

bool* isActive = nullptr;
Vec3* positions = nullptr;
Vec4* rotations = nullptr;
Vec3* scales = nullptr;
char* names = nullptr;
char* meshNames = nullptr;

void init()
{
    
    entitySize = entityDefaultCount;
    entitySizeCache = entitySize;
	//create the scene entity EntityArena
    sceneEntities = createEntityArena(&SceneEntity, entitySizeCache);
  
    printf("Entity Arena created size: %d\n",entitySizeCache);
    
    positions = (Vec3*)sceneEntities->fields[0];
    rotations = (Vec4*)sceneEntities->fields[1];
    scales = (Vec3*)sceneEntities->fields[2];
    isActive = (bool*)sceneEntities->fields[3];
    names = (char*)sceneEntities->fields[4];
    meshNames = (char*)sceneEntities->fields[5];
    //set to empty strings
    memset(meshNames, 0, entitySize * MAX_MESH_NAME_SIZE);

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

    //compile simple lighting shader that exists in the testbed resources folder
    shader = createGraphicsProgram("../res/shader.vert",
        "../res/shader.frag");

    cubeMesh = createBoxMesh();
    Mesh* monkey = loadModel("../res/models/monkey3.obj");
    Mesh* warhammer = loadModel("../res/models/Pole_Warhammer.fbx");
    Mesh* shield = loadModel("../res/models/Shield_Crusader.fbx");

	monkey->material.unifroms = getMaterialUniforms(shader);
    warhammer->material.unifroms = getMaterialUniforms(shader);
    shield->material.unifroms = getMaterialUniforms(shader);
    cubeMesh->material.unifroms = getMaterialUniforms(shader);


    //setup camera that looks at the origin from z = +5
    initCamera(&sceneCam,
               (Vec3){0.0f, 0.0f, 5.0f},   //position
               70.0f,             //field of view
               1.0f,                       //aspect (real aspect fixed every frame)
               0.1f, 100.0f);              //near/far clip planes


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

    addMesh(cubeMesh, "Cube");
    addMesh(monkey,"Monkey");
    addMesh(warhammer, "Warhammer");
    addMesh(shield, "Shield");

}

void update(f32 dt)
{
    if(entitySize != entitySizeCache)
    {
        printf("changed size %d", entitySize);
        //this means we need to re allocate the Entity EntityArena
        reallocateSceneArena();
        //set the cache
        entitySizeCache = entitySize;
    }
	//per-frame simulation update and camera motion
	//spin the cube so we have something moving
    rotationAngle += (45.0f)* dt;
    cubeXform.rot = quatFromAxisAngle(v3Up, radians(rotationAngle));
        
    if(canMoveViewPort)
    {
        moveViewPortCamera(dt);
    }
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

    free(meshMap);
    freeEntityArena(sceneEntities);
    //free editor resources before exiting
    freeMesh(cubeMesh);
    freeShader(shader);

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
	editor->inputProcess = processInput;	
    meshMap = createMeshMap(16);	
    run(editor); 
	return 0;
}
