#include "../../include/druid.h"


//perforamance and frame rate tracking
f64 performanceFreq = 0.0;
u64 previousTime = 0;
f32 frameCount = 0;

f64 FPS = 0.0;
u32 fps = 0;

//create the application
Application* createApplication(FncPtr init, FncPtrFloat update, FncPtrFloat render, FncPtr destroy)
{
	//create the application
	Application* app = (Application*)malloc(sizeof(Application));

	//assert that the application was created
	assert(app != NULL && "Application could not be created");
	
	//setup app state
	app->state = RUN;
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
    //destroy the display
	onDestroy(app->display);
    //free app data	
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

void initSystems(const Application* app)
{
    //get default values for the display
	f32 width = app->width == 0 ? 1920 : app->width;
	f32 height = app->height == 0 ? 1080 : app->height;
	//initialize the display
	initDisplay(app->display,width, height);

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
		//calculate delta time for this frame (in seconds)
		u64 current = SDL_GetPerformanceCounter();
		f32 dt = (f32)((f64)(current - previousTime) / performanceFreq);
		previousTime = current;

		//process input if handler provided
		if (app->inputProcess)
			app->inputProcess(app);

		//update & render – pass freshly computed dt
		app->update(dt);
		render(app, dt);

		//fps counter
		frameCount++;
		f64 elapsedTime = (double)(current - fpsTime) / performanceFreq;
		if (elapsedTime >= 1.0)
		{
			FPS = frameCount / elapsedTime;
			frameCount = 0;
			fpsTime = current;
		}
		app->fps = FPS;
	}
}



void render(Application* app, float dt)
{
	clearDisplay(0.0f, 0.0f, 0.0f, 1.0f);
	
	app->render(dt);

	swapBuffer(app->display);
} 
