#include "../../include/druid.h"
#include <iostream>


double performanceFreq = 0.0;
u64 previousTime = 0;
float frameCount = 0;

double FPS = 0.0;
int fps = 0;
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
	
	
	app->init = init;
	app->update = update;
	app->render = render;
	app->destroy = destroy;

	
	return app;
}

void destroyApplication(Application* app)
{
	onDestroy(app->display);
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
	
	//initialize the display
	initDisplay(app->display);

	app->init();

	//setup timer
	previousTime = SDL_GetPerformanceCounter();
	performanceFreq = (double)SDL_GetPerformanceFrequency();

}






void startApplication(Application* app)
{
	u64 current = SDL_GetPerformanceCounter();
      	float dt = (float)((double)(current - previousTime) / performanceFreq);
	
	u64 fpsTime = current;
	previousTime = current;
	double elapsedTime = (double)(current - previousTime);	
	while (app->state != EXIT)
	{


	

		processInput(app);

		//update the application
		app->update(dt);
		//render the application
		render(app,dt);
	
		frameCount++;
		current = SDL_GetPerformanceCounter();
		
		elapsedTime = (double)(current - fpsTime) / performanceFreq;
		
		if(elapsedTime >= 1.0)
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
	
				
	glEnableClientState(GL_COLOR_ARRAY); 
	glEnd();

	swapBuffer(app->display);
} 
