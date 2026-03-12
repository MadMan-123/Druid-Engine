// Input handling for Druid engine
// Provides keyboard and mouse input utilities
#include "../../include/druid.h"

static SDL_Event evnt;
static const b8* state; 
SDL_Gamepad* gamepads[GAMEPAD_MAX] = { NULL }; //array to hold gamepads
u32 gamepadCount = 0; //number of gamepads connected

f32 xInputAxis = 0.0f; //x axis input from keyboard or gamepad
f32 yInputAxis = 0.0f; //y axis input from keyboard or gamepad
f32 xLookAxis = 0.0f;  //x look axis from mouse or gamepad
f32 yLookAxis = 0.0f;  //y look axis from mouse or gamepad

// Global flag set by editor when ImGui captures input
static b8 imguiCapturingInput = false;

#define INPUT_AXIS_DEADZONE 0.2f
#define LOOK_AXIS_GAMEPAD_SCALE 18.0f

static SDL_Gamepad *getPrimaryGamepad(void)
{
	for (u32 i = 0; i < GAMEPAD_MAX; i++)
	{
		if (gamepads[i] != NULL)
			return gamepads[i];
	}

	return NULL;
}

static f32 applyAxisDeadZone(f32 value, f32 deadZone)
{
	f32 mag = fabsf(value);
	if (mag <= deadZone)
		return 0.0f;

	f32 sign = value < 0.0f ? -1.0f : 1.0f;
	f32 scaled = (mag - deadZone) / (1.0f - deadZone);
	return clamp(sign * scaled, -1.0f, 1.0f);
}

static Vec2 getPrimaryJoystickAxis(JoystickCode axis1, JoystickCode axis2)
{
	Vec2 axis = {0};
	SDL_Gamepad *pad = getPrimaryGamepad();
	if (!pad)
		return axis;

	axis.x = -(f32)SDL_GetGamepadAxis(pad, axis1) / 32767.0f;
	axis.y = -(f32)SDL_GetGamepadAxis(pad, axis2) / 32767.0f;
	axis.x = applyAxisDeadZone(axis.x, INPUT_AXIS_DEADZONE);
	axis.y = applyAxisDeadZone(axis.y, INPUT_AXIS_DEADZONE);
	return axis;
}


void initInput()
{
	//initialize SDL event system
	SDL_Init(SDL_INIT_EVENTS);
	
	//initialize the keyboard state
	state = SDL_GetKeyboardState(NULL);
	
	//initialize gamepads currently connected
	int connectedCount = 0;
	SDL_JoystickID *connected = SDL_GetGamepads(&connectedCount);
	if (connected)
	{
		for (int i = 0; i < connectedCount; i++)
		{
			if (gamepadCount >= GAMEPAD_MAX)
				break;

			SDL_Gamepad *gamepad = SDL_OpenGamepad(connected[i]);
			if (!gamepad)
				continue;

			for (u32 slot = 0; slot < GAMEPAD_MAX; slot++)
			{
				if (gamepads[slot] == NULL)
				{
					gamepads[slot] = gamepad;
					gamepadCount++;
					DEBUG("Gamepad connected in slot %u (id=%d)\n", slot, (int)connected[i]);
					break;
				}
			}
		}
		SDL_free(connected);
	}
	
	
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
	SDL_JoystickID instanceID = event->gdevice.which;

	for (u32 i = 0; i < GAMEPAD_MAX; i++)
	{
		if (gamepads[i] && SDL_GetGamepadID(gamepads[i]) == instanceID)
			return;
	}

	if (gamepadCount >= GAMEPAD_MAX)
	{
		WARN("Gamepad connected but no free slots (id=%d)", (int)instanceID);
		return;
	}

	SDL_Gamepad* gamepad = SDL_OpenGamepad(instanceID);
	if (!gamepad)
	{
		ERROR("Failed to open gamepad id=%d\n", (int)instanceID);
		return;
	}

	for (u32 i = 0; i < GAMEPAD_MAX; i++)
	{
		if (gamepads[i] == NULL)
		{
			gamepads[i] = gamepad;
			gamepadCount++;
			DEBUG("Gamepad connected in slot %u (id=%d)\n", i, (int)instanceID);
			return;
		}
	}

	SDL_CloseGamepad(gamepad);
}

void checkForGamepadRemoved(SDL_Event* event)
{
	//check if a gamepad has been disconnected
	SDL_JoystickID instanceID = event->gdevice.which;
	for (u32 i = 0; i < GAMEPAD_MAX; i++)
	{
		if (gamepads[i] && SDL_GetGamepadID(gamepads[i]) == instanceID)
		{
			DEBUG("Gamepad disconnected from slot %u (id=%d)\n", i, (int)instanceID);
			SDL_CloseGamepad(gamepads[i]);
			gamepads[i] = NULL;
			if (gamepadCount > 0)
				gamepadCount--;
			return;
		}
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
			case SDL_EVENT_GAMEPAD_ADDED:
				checkForGamepadConnection(&evnt);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				checkForGamepadRemoved(&evnt);
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

void updateInputAxes(void)
{
	if (imguiCapturingInput)
	{
		xInputAxis = 0.0f;
		yInputAxis = 0.0f;
		xLookAxis = 0.0f;
		yLookAxis = 0.0f;
		return;
	}

	Vec2 keyboardAxis = getKeyboardAxis();
	Vec2 moveStickAxis = getPrimaryJoystickAxis(JOYSTICK_LEFT_X, JOYSTICK_LEFT_Y);
	Vec2 lookStickAxis = getPrimaryJoystickAxis(JOYSTICK_RIGHT_X, JOYSTICK_RIGHT_Y);
	f32 mouseX = 0.0f;
	f32 mouseY = 0.0f;

	getMouseDelta(&mouseX, &mouseY);

	xInputAxis = clamp(keyboardAxis.x + moveStickAxis.x, -1.0f, 1.0f);
	yInputAxis = clamp(keyboardAxis.y + moveStickAxis.y, -1.0f, 1.0f);

	xLookAxis = mouseX + (lookStickAxis.x * LOOK_AXIS_GAMEPAD_SCALE);
	yLookAxis = mouseY + (lookStickAxis.y * LOOK_AXIS_GAMEPAD_SCALE);
}

Vec2 getInputAxis(void)
{
	return (Vec2){ xInputAxis, yInputAxis };
}

Vec2 getLookAxis(void)
{
	return (Vec2){ xLookAxis, yLookAxis };
}

f32 getInputAxisX(void)
{
	return xInputAxis;
}

f32 getInputAxisY(void)
{
	return yInputAxis;
}

f32 getLookAxisX(void)
{
	return xLookAxis;
}

f32 getLookAxisY(void)
{
	return yLookAxis;
}



/// Check if a key is pressed

b8 isKeyDown(KeyCode key)
{
	state = SDL_GetKeyboardState(NULL);
	return state[key]; 
}

b8 isKeyUp(KeyCode key)
{
	state = SDL_GetKeyboardState(NULL);
	return !state[key]; 
}

b8 isButtonDown(u32 controllerID, ControllerCode button)
{
	if (imguiCapturingInput)
		return false;

	SDL_Gamepad* pad = gamepads[controllerID];
	if(pad == NULL) 
	{
		DEBUG("No gamepad found at index %d\n", controllerID);
		return false; //no gamepad found
	}
	return SDL_GetGamepadButton(pad, button);
}


/// Check if a mouse button is pressed
b8 isMouseDown(u32 button)
{
	if (imguiCapturingInput)
		return false;
	return (SDL_GetMouseState(NULL,NULL) & SDL_BUTTON_MASK(button)) != 0;
}
/// Get the mouse position
void getMouseDelta(f32*x,f32*y)
{
	SDL_GetRelativeMouseState(x,y); 
}

/// Set input capture state (called by editor when ImGui wants input)
void setInputCaptureState(b8 captured)
{
	imguiCapturingInput = captured;
}



