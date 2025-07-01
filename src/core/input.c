// Input handling for Druid engine
// Provides keyboard and mouse input utilities
#include "../../include/druid.h"

static SDL_Event evnt;
static const bool* state; 

void processInput(Application* app)
{
    //tell SDL to process events
	SDL_PumpEvents();

    //get the current state of the keyboard
	while(SDL_PollEvent(&evnt)) //get and process events
	{
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



/// Check if a key is pressed

bool isInputDown(KeyCode key)
{
	state = SDL_GetKeyboardState(NULL);
	return state[key]; 
}

/// Check if a mouse button is pressed
bool isMouseDown(u32 button)
{
	return (SDL_GetMouseState(NULL,NULL) & SDL_BUTTON_MASK(button)) != 0;
}
/// Get the mouse position
void getMouseDelta(f32*x,f32*y)
{
	SDL_GetRelativeMouseState(x,y); 
}



