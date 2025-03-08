#pragma once
#include "display.h"

typedef struct{
	Display display;	
}GraphicsState;


DAPI GraphicsState* createGraphicsState();
DAPI void cleanUpGraphicsState(GraphicsState* state);

