// Input handling for Druid engine
// Provides keyboard and mouse input utilities
#include "../../include/druid.h"

static SDL_Event evnt;
static const bool* state; 
SDL_Gamepad* gamepads[GAMEPAD_MAX] = { NULL }; //array to hold gamepads
u32 gamepadCount = 0; //number of gamepads connected

DAPI f32 xInputAxis = 0.0f; //x axis input from keyboard or gamepad
DAPI f32 yInputAxis = 0.0f; //y axis input from keyboard or gamepad


void initInput()
{
	//initialize SDL event system
	SDL_Init(SDL_INIT_EVENTS);
	
	//initialize the keyboard state
	state = SDL_GetKeyboardState(NULL);
		
	//initialize gamepads
	for (u32 i = 0; i < GAMEPAD_MAX; i++) 
	{
		//get valid gamepads
		SDL_Gamepad* gamepad = SDL_OpenGamepad(i);
		if(gamepad == NULL) 
		{
			goto skip; //if no gamepad found then skip
		} 
		else 
		{
			gamepads[i] = gamepad; //store the gamepad
			DEBUG("Gamepad %d initialized\n", i);
			gamepadCount++;
		}

	}
	
skip:
	
}

void destroyInput()
{
	//close the SDL event system
	SDL_QuitSubSystem(SDL_INIT_EVENTS);
	
	//close gamepads
	for (u32 i = 0; i < GAMEPAD_MAX; i++) 
	{
		if (gamepads[i] != NULL) 
		{
			SDL_CloseGamepad(gamepads[i]);
			gamepads[i] = NULL;
		}
	}
}
void checkForGamepadConnection(SDL_Event *event)
{
		//check if a gamepad has been connected
		u32 controllerID = event->gdevice.which;
		if (controllerID < GAMEPAD_MAX && gamepads[controllerID] == NULL) 
		{
			SDL_Gamepad* gamepad = SDL_OpenGamepad(controllerID);
			if (gamepad != NULL) 
			{
				gamepads[controllerID] = gamepad;
				gamepadCount++;
				DEBUG("Gamepad %d connected\n", controllerID);
			} 
			else 
			{
				ERROR("Failed to open gamepad %d\n", controllerID);
			}
		}
}

void checkForGamepadRemoved(SDL_Event* event)
{
	//check if a gamepad has been disconnected
		u32 controllerID = event->gdevice.which;
		if (controllerID < GAMEPAD_MAX && gamepads[controllerID] != NULL) 
		{
			DEBUG("Gamepad %d disconnected\n", controllerID);
			SDL_CloseGamepad(gamepads[controllerID]);
			gamepads[controllerID] = NULL;
			gamepadCount--;
		}
}

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



Vec2 getKeyboardAxis()
{
	Vec2 axis = { 0 };
	
	//if the D key is pressed make x +1 else if A then make x -1
	f32 xP = isKeyDown(KEY_D) ? 1 : 0;
	f32 xN = isKeyDown(KEY_A) ? -1 : 0;

	f32 yP = isKeyDown(KEY_W) ? 1 : 0;
	f32 yN = isKeyDown(KEY_S) ? -1 : 0;

	axis.x = xP + xN;
	axis.y = yP + yN;
	axis.x = -axis.x;
	return axis;
}

Vec2 getJoystickAxis(u32 controllerID, JoystickCode axis1, JoystickCode axis2)
{
	Vec2 axis = { 0 };

	//get valid gamepad
	SDL_Gamepad* pad = gamepads[controllerID];
	//check if the gamepad is valid
	if(pad == NULL) 
	{
		//DEBUG("No gamepad found at index %d\n", controllerID);
		return axis; //no gamepad found
	}

	//get the left joystick axi
	axis.x = -SDL_GetGamepadAxis(pad, axis1);
	axis.y = -SDL_GetGamepadAxis(pad, axis2);
	//normalize the axi
	axis.x /= 32767.0f; //SDL joystick axis range is -32768 to 32767
	axis.y /= 32767.0f; //normalize to -1 to 1 range
	return axis;
}



/// Check if a key is pressed

bool isKeyDown(KeyCode key)
{
	state = SDL_GetKeyboardState(NULL);
	return state[key]; 
}

bool isKeyUp(KeyCode key)
{
	state = SDL_GetKeyboardState(NULL);
	return !state[key]; 
}

bool isButtonDown(u32 controllerID, ControllerCode button)
{
	SDL_Gamepad* pad = gamepads[controllerID];
	if(pad == NULL) 
	{
		DEBUG("No gamepad found at index %d\n", controllerID);
		return false; //no gamepad found
	}
	return SDL_GetGamepadButton(pad, button);
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



