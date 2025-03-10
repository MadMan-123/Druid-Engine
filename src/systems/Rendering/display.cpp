#include "display.h"


void returnError(const std::string& errorString)
{
	std::cout << errorString << '\n';
	std::cout << "press any  key to quit...";
	std::cin.get();
	SDL_Quit();
}

void swapBuffer(const Display* display)
{
	SDL_GL_SwapWindow(display->sdlWindow); //swap buffers
}

void clearDisplay(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear colour and depth buffer - set colour to colour defined in glClearColor
}

void initDisplay(Display* display)
{
	
	display->sdlWindow = nullptr; //initialise to generate null access violation for debugging. 
	display->screenWidth = 1024.0f;
	display->screenHeight = 768.0f; 
	SDL_Init(SDL_INIT_EVENTS); //initalise everything

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8); //Min no of bits used to diplay colour
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);// set up z-buffer
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); // set up double buffer   

	display->sdlWindow = SDL_CreateWindow("Game Window",(int)display->screenWidth, (int)display->screenHeight, SDL_WINDOW_OPENGL); // create window
	

	if (display->sdlWindow == nullptr)
	{
		returnError("window failed to create");
	}

	display->glContext = SDL_GL_CreateContext(display->sdlWindow);

	if (display->glContext == nullptr)
	{
		returnError("SDL_GL context failed to create");
	}

	GLenum error = glewInit();
	if (error != GLEW_OK)
	{
		returnError("GLEW failed to initialise");
	}

	glEnable(GL_DEPTH_TEST); //enable z-buffering 
	glEnable(GL_CULL_FACE); //dont draw faces that are not pointing at the camera

	glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
}


void onDestroy(Display* display)
{
		SDL_GL_DestroyContext(display->glContext); // delete context
    	SDL_DestroyWindow(display->sdlWindow); // detete window (make sure to delete the context and the window in the opposite order of creation in initDisplay())
    	SDL_Quit();
		free(display);
}
