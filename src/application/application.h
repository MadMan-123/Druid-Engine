#pragma once
#include <SDL3\SDL.h>
#include <GL/glew.h>
#include "../systems/rendering/display.h" 
#include "../systems/rendering/texture.h" 
#include "../core/transform.h" 
#include "../systems/rendering/mesh.h" 
#include "../systems/rendering/shader.h" 

enum ApplicationState{RUN, EXIT};

extern double FPS;
typedef struct 
{
	//function pointers
	void(*init)();
	void(*update)(float);
	void(*render)(float);
	void(*destroy)();
	
	//open gl context with sdl within the display	
	Display* display;
	ApplicationState state;
	double fps;		
}Application;

//function pointer typedef
typedef void(*FncPtrFloat)(float);
typedef void(*FncPtr)();

DAPI Application* createApplication(FncPtr init, FncPtrFloat update, FncPtrFloat render, FncPtr destroy);
DAPI void run(Application* app);
DAPI void destroyApplication(Application* app);


//methods to make the application run
DAPI void initSystems(const Application* app);
DAPI void startApplication(Application* app);

DAPI void render(Application* app,float dt);
