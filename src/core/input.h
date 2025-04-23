#pragma once
#include <SDL3\SDL.h>
#include "../application/application.h"
#include "keys.h"
#include "../defines.h"

//Big old list of defines
//



void processInput(Application* app);

DAPI bool isInputDown(KeyCode key);

DAPI bool isMouseDown(u32 button);

DAPI void getMouseDelta(f32*x, f32*y);

