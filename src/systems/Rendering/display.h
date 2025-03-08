#pragma once
#include <SDL3/SDL.h>
#include <GL\glew.h>
#include <iostream>
#include <string>

#include "../../defines.h"


typedef struct 
{
	SDL_GLContext glContext; //global variable to hold the context
	SDL_Window* sdlWindow; //holds pointer to out window
	float screenWidth;
	float screenHeight;
}Display;

DAPI void initDisplay(Display* display);
DAPI void swapBuffer(const Display* display);
DAPI void clearDisplay(float r, float g, float b, float a);


DAPI void returnError(const std::string& errorString);
DAPI void onDestroy(Display* display);
