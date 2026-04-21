#include "../../include/druid.h"

static SDL_Event evnt;
static const b8* state;
SDL_Gamepad* gamepads[GAMEPAD_MAX] = { NULL };
u32 gamepadCount = 0;

f32 xInputAxis = 0.0f;
f32 yInputAxis = 0.0f;
f32 xLookAxis = 0.0f;
f32 yLookAxis = 0.0f;

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
	// SDL_Init already called by initDisplay  do not re-init here
	state = SDL_GetKeyboardState(NULL);

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
	SDL_QuitSubSystem(SDL_INIT_EVENTS);

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
	SDL_PumpEvents();

	while(SDL_PollEvent(&evnt))
	{
		switch (evnt.type)
		{
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

	SDL_Gamepad* pad = gamepads[controllerID];
	if(pad == NULL)
		return axis;

	axis.x = -SDL_GetGamepadAxis(pad, axis1);
	axis.y = -SDL_GetGamepadAxis(pad, axis2);
	axis.x /= 32767.0f;
	axis.y /= 32767.0f;
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

	xInputAxis = clamp(-keyboardAxis.x + -moveStickAxis.x, -1.0f, 1.0f);
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

b8 isButtonUp(u32 controllerID, ControllerCode button)
{
	if (imguiCapturingInput)
		return false;

	SDL_Gamepad* pad = gamepads[controllerID];
	if(pad == NULL) 
	{
		DEBUG("No gamepad found at index %d\n", controllerID);
		return false; //no gamepad found
	}
	return !SDL_GetGamepadButton(pad, button);
}



b8 isMouseDown(u32 button)
{
	if (imguiCapturingInput)
		return false;
	return (SDL_GetMouseState(NULL,NULL) & SDL_BUTTON_MASK(button)) != 0;
}
void getMouseDelta(f32*x,f32*y)
{
	SDL_GetRelativeMouseState(x,y); 
}

void setInputCaptureState(b8 captured)
{
	imguiCapturingInput = captured;
}

static b8 mouseCaptured = false;

void setMouseCaptured(b8 captured)
{
	if (renderer && renderer->display && renderer->display->sdlWindow)
	{
		SDL_SetWindowRelativeMouseMode(renderer->display->sdlWindow, captured);
		mouseCaptured = captured;
	}
}

b8 isMouseCaptured(void)
{
	return mouseCaptured;
}



