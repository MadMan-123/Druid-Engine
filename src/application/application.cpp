#include "Application.h"
#include <iostream>




Application* createApplication(FncPtr init, FncPtr update, FncPtr render, FncPtr destroy)
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
}






void startApplication(Application* app)
{
	while (app->state != EXIT)
	{
		processInput(app);

		//update the application
		app->update();
		//render the application
		render(app);
	}
}

void processInput(Application* app)
{
	SDL_Event evnt;

	// allow the camera to move based on input
	app->input = SDL_GetKeyboardState(NULL);
	while(SDL_PollEvent(&evnt)) //get and process events
	{
		switch (evnt.type)
		{
			case SDL_EVENT_QUIT:
				app->state = EXIT;
				break;
			default: ;
		}
	}
	
}

void render(Application* app)
{
	clearDisplay(0.0f, 0.0f, 0.0f, 1.0f);
	
	app->render();
	
				
	glEnableClientState(GL_COLOR_ARRAY); 
	glEnd();

	swapBuffer(app->display);
} 
