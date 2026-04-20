#include "../../include/druid.h"

f64 performanceFreq = 0.0;
u64 previousTime = 0;
f32 frameCount = 0;
f64 FPS = 0.0;
u32 fps = 0;

void inputUpdate(Application* app)
{
	if (app->inputProcess)
		app->inputProcess(app);
	updateInputAxes();
}

Application* createApplication(const c8* title, FncPtr init, FncPtrFloat update, FncPtrFloat render, FncPtr destroy)
{
	Application* app = (Application*)calloc(1, sizeof(Application));
	assert(app != NULL && "Application could not be created");

	app->state = RUN;

	assert(init    != NULL && "Init function pointer is null");
	assert(update  != NULL && "Update function pointer is null");
	assert(render  != NULL && "Render function pointer is null");
	assert(destroy != NULL && "Destroy function pointer is null");

	strncpy(app->title, title, MAX_PATH_LENGTH - 1);
	((c8 *)app->title)[MAX_PATH_LENGTH - 1] = '\0';

	app->init    = init;
	app->update  = update;
	app->render  = render;
	app->destroy = destroy;

	return app;
}

void destroyApplication(Application* app)
{
    app->destroy();
    onDestroy(display);
    SDL_Quit();
    shutdownLogging();
    cleanUpResourceManager(resources);
    memorySystemShutdown();
    free(app);
}

void run(Application* app)
{
	assert(app != NULL && "Application is null");
	initSystems(app);
	startApplication(app);
}


#define MATERIAL_COUNT 8192
#define TEXTURE_COUNT  2048
#define MESH_COUNT     8192
#define MODEL_COUNT    2048
#define SHADER_COUNT   128

void initSystems(const Application* app)
{
    f32 width  = app->width  == 0 ? 1920 : app->width;
    f32 height = app->height == 0 ? 1080 : app->height;

	MemoryConfig memCfg = memDefaultConfig();
	if (!memLoadConfig("memory.conf", &memCfg))
		memLoadConfig("../memory.conf", &memCfg);
	memorySystemInit(&memCfg);

	initLogging();
	resources = createResourceManager(
		MATERIAL_COUNT, TEXTURE_COUNT, MESH_COUNT, MODEL_COUNT, SHADER_COUNT);
	assert(resources != NULL && "Resource Manager not created correctly");

	profileCalibrate();

	initDisplay(app->title, width, height);

	resources->geoBuffer = geometryBufferCreate(2000000, 6000000);
	if (!resources->geoBuffer)
		WARN("GeometryBuffer creation failed, meshes will use standalone VAO/VBO/EBO");

	const c8 *resPath = fileExists("./"RES_FOLDER"shader.vert") ? "./"RES_FOLDER : "../"RES_FOLDER;
	readResources(resources, resPath);

	app->init();

	previousTime = SDL_GetPerformanceCounter();
	performanceFreq = (f64)SDL_GetPerformanceFrequency();
}

void startApplication(Application* app)
{
	assert(app != NULL && "Application is null");

	previousTime = SDL_GetPerformanceCounter();
	u64 fpsTime  = previousTime;
	while (app->state != EXIT)
	{
		frameReset();
		profileBeginFrame();

		u64 current = SDL_GetPerformanceCounter();
		f32 dt = (f32)((f64)(current - previousTime) / performanceFreq);
		previousTime = current;

		{ PROFILE_SCOPE("Input");  inputUpdate(app); }
		{ PROFILE_SCOPE("Update"); app->update(dt);  }
		{ PROFILE_SCOPE("Render"); applicationRenderStep(app, dt); }

		profileEndFrame();

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

	destroyApplication(app);
}

void applicationRenderStep(Application* app, f32 dt)
{
	clearDisplay(0.0f, 0.0f, 0.0f, 1.0f);
	app->render(dt);
	swapBuffer(renderer ? renderer->display : display);
}
