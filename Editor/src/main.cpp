// Bare-bones Editor main.cpp
#include <iostream>
#include <druid.h>
#include "..\deps\imgui\imgui.h"
#include "..\deps\imgui\imgui_impl_sdl3.h"
#include "..\deps\imgui\imgui_impl_opengl3.h"


Application* editor;
static SDL_Event evnt;


void processInput(void* appData)
{
	//void* should be Application
	Application* app = (Application*)appData;
    	//tell SDL to process events
	SDL_PumpEvents();

    	//get the current state of the keyboard
	while(SDL_PollEvent(&evnt)) //get and process events
	{
		//pass imgui events		
		ImGui_ImplSDL3_ProcessEvent(&evnt);
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

void init()
{
	// After SDL window and OpenGL context creation:
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	ImGui::StyleColorsDark();

	// Initialize ImGui backends
	ImGui_ImplSDL3_InitForOpenGL(editor->display->sdlWindow, editor->display->glContext);
	ImGui_ImplOpenGL3_Init("#version 330"); // Or your GL version string

	editor->inputProcess = processInput;	
}

void update(f32 dt)
{
	
}

void render(f32 dt)
{
	
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();

	ImGui::NewFrame();

	// Your ImGui code here
	ImGui::Begin("Editor");
	ImGui::Text("This is your editor!");
	if(ImGui::Button("Click this"))
	{
		printf("I be clickin n shit\n");
	}

	ImGui::ShowDemoWindow();
	
	ImGui::End();

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void destroy()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}



int main(int argc, char** argv) 
{
	editor = createApplication(init, update, render, destroy);
	editor->width = 1920;
	editor->height = 1080;	
		
	run(editor); 
	return 0;
}
