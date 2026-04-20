#include "../../../include/druid.h"

static Display s_display = {0};
Display *display = NULL;

void returnError(const c8* errorString)
{
	printf("[DISPLAY ERROR]%s/n",errorString);
	printf("press any key to quit...\n");
	getchar();
	SDL_Quit();
}

void swapBuffer(const Display* display)
{
	SDL_GL_SwapWindow(display->sdlWindow);
}

void clearDisplay(f32 r, f32 g, f32 b, f32 a)
{
	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void initDisplay(const c8* title, f32 width, f32 height)
{
    display = &s_display;
    display->sdlWindow   = NULL;
    display->screenWidth  = width;
    display->screenHeight = height;

    SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,    8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,  8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,   8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,  16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    display->sdlWindow = SDL_CreateWindow(title,
        (i32)display->screenWidth, (i32)display->screenHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!display->sdlWindow)
        returnError("window failed to create");

    display->glContext = SDL_GL_CreateContext(display->sdlWindow);
    if (!display->glContext)
        returnError("SDL_GL context failed to create");

    GLenum error = glewInit();
    if (error != GLEW_OK)
        returnError("GLEW failed to initialise");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
}

void setVSync(i32 interval)
{
    if (!SDL_GL_SetSwapInterval(interval))
    {
        WARN("setVSync: SDL_GL_SetSwapInterval(%d) failed: %s", interval, SDL_GetError());
        if (interval == -1)
            SDL_GL_SetSwapInterval(1);
    }
}

void onDestroy(Display* d)
{
    SDL_GL_DestroyContext(d->glContext);
    SDL_DestroyWindow(d->sdlWindow);
    // display is &s_display (static) — no free
}
