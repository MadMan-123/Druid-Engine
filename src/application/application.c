#include "../../include/druid.h"

//perforamance and frame rate tracking
f64 performanceFreq = 0.0;
u64 previousTime = 0;
f32 frameCount = 0;
f64 FPS = 0.0;
u32 fps = 0;

void inputUpdate(Application* app)
{
	//process input if handler provided
		if (app->inputProcess)
			app->inputProcess(app);

	updateInputAxes();
}
//create the application
Application* createApplication(const c8* title,FncPtr init, FncPtrFloat update, FncPtrFloat render, FncPtr destroy)
{
	//create the application
	Application* app = (Application*)calloc(1, sizeof(Application));

	//assert that the application was created
	assert(app != NULL && "Application could not be created");

	//setup app state
	app->state = RUN;


	//TODO: seperate the display from the application,
	// instead there should be a GraphicsState or Renderer struct that holds the display and all graphics related data.
	
	//create the display (open gl context, SDl window, glew)
	app->display = (Display*)malloc(sizeof(Display));
	//assert that the display was created
	assert(app->display != NULL && "Display could not be created");

	//set the function pointers

	//assert that the function pointers are not null
	assert(init != NULL && "Init function pointer is null");
	assert(update != NULL &&  "Update function pointer is null");
	assert(render != NULL && "Render function pointer is null");
	assert(destroy != NULL && "Destroy function pointer is null");
	
	// copy the title to the application
	strncpy(app->title, title, MAX_PATH_LENGTH - 1);

	// set the null terminator for the title
	((c8 *)app->title)[MAX_PATH_LENGTH - 1] = '\0';

    //set the function pointers
	app->init = init;
	app->update = update;
	app->render = render;
	app->destroy = destroy;

	//return the application
	return app;
}

//destroy the application
void destroyApplication(Application* app)
{
    //call the developer's destroy function first 
	app->destroy();
    
    //destroy the display
	onDestroy(app->display);
    
    //shutdown SDL after ImGui has been properly cleaned up
    SDL_Quit();
    
    //free app data
	shutdownLogging();
	cleanUpResourceManager(resources);
	memorySystemShutdown();
    free(app);
}

void run(Application* app)
{
	//assert that the application is not null
	assert(app != NULL && "Application is null");

	//initialize everything needed before the application starts
	initSystems(app);
	//start the application
	startApplication(app);
}


#define MATERIAL_COUNT 8192
#define TEXTURE_COUNT 2048
#define MESH_COUNT 8192
#define MODEL_COUNT 2048
#define SHADER_COUNT 128

void initSystems(const Application* app)
{
    //get default values for the display
	f32 width = app->width == 0 ? 1920 : app->width;
	f32 height = app->height == 0 ? 1080 : app->height;

	// initialize memory system, try project config first
	MemoryConfig memCfg = memDefaultConfig();
	if (!memLoadConfig("memory.conf", &memCfg))
		memLoadConfig("../memory.conf", &memCfg);
	memorySystemInit(&memCfg);

	initLogging(); //initialize logging system
	resources = createResourceManager(
		MATERIAL_COUNT,
		TEXTURE_COUNT,
		MESH_COUNT,
		MODEL_COUNT,
		SHADER_COUNT
	); 
	
	//null check the resource manager
	assert(resources != NULL && "Resource Manager not created correctly");
	
	profileCalibrate();

	//initialize the display
	initDisplay(app->title, app->display, width, height);

	// create geometry buffer after OpenGL context, before mesh loading
	resources->geoBuffer = geometryBufferCreate(2000000, 6000000);
	if (!resources->geoBuffer)
		WARN("GeometryBuffer creation failed, meshes will use standalone VAO/VBO/EBO");

	// load resources, prefer ./res/
	// Fall back to ../res/ (development, exe in bin/, res/ at project root)
	const c8 *resPath = fileExists("./"RES_FOLDER"shader.vert") ? "./"RES_FOLDER : "../"RES_FOLDER;
	readResources(resources, resPath);
    //call the init function pointer
	app->init();

	//setup timer
	previousTime = SDL_GetPerformanceCounter();
	performanceFreq = (f64)SDL_GetPerformanceFrequency();

}



void startApplication(Application* app)
{
	//prime timers
	previousTime = SDL_GetPerformanceCounter();
	u64 fpsTime   = previousTime;
	while (app->state != EXIT)
	{
		frameReset();
		profileBeginFrame();

		//calculate delta time for this frame (in seconds)
		u64 current = SDL_GetPerformanceCounter();
		f32 dt = (f32)((f64)(current - previousTime) / performanceFreq);
		previousTime = current;

		{ PROFILE_SCOPE("Input");  inputUpdate(app); }
		{ PROFILE_SCOPE("Update"); app->update(dt);  }
		{ PROFILE_SCOPE("Render"); applicationRenderStep(app, dt);  }

		profileEndFrame();

		//fps counter
		frameCount++;
		f64 elapsedTime = (f64)(current - fpsTime) / performanceFreq;
		if (elapsedTime >= 1.0)
		{
			FPS = frameCount / elapsedTime;
			frameCount = 0;
			fpsTime = current;
		}
		app->fps = FPS;
	}

	//destroy the app
	destroyApplication(app);
}



void applicationRenderStep(Application* app, f32 dt)
{
	clearDisplay(0.0f, 0.0f, 0.0f, 1.0f);
	
	app->render(dt);

	swapBuffer(app->display);
} 
