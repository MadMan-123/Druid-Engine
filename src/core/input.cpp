#include "../../include/druid.h"

static SDL_Event evnt;
static const bool* state = SDL_GetKeyboardState(NULL);

void processInput(Application* app)
{
	SDL_PumpEvents();


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




bool isInputDown(KeyCode key)
{
	state = SDL_GetKeyboardState(NULL);
	return state[key]; 
}


bool isMouseDown(u32 button)
{
	return (SDL_GetMouseState(NULL,NULL) & SDL_BUTTON_MASK(button)) != 0;
}

void getMouseDelta(f32*x,f32*y)
{
	SDL_GetRelativeMouseState(x,y); 
}



