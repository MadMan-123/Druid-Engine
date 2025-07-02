// Bare-bones Editor main.cpp
#include <iostream>
#include <druid.h>
#include "..\deps\imgui\imgui.h"
#include "..\deps\imgui\imgui_impl_sdl3.h"
#include "..\deps\imgui\imgui_impl_opengl3.h"
#include "..\deps\imgui\imgui_internal.h"


Application* editor;
static SDL_Event evnt;
SDL_Joystick* joystick = NULL;

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
    	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
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

	static bool dockspaceOpen = true;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);
	window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::Begin("MainDockSpace", &dockspaceOpen, window_flags);
	ImGui::PopStyleVar(3);

	ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceID");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

	// --- DockBuilder: Set up default layout once ---
	static bool first_time = true;
	if (first_time)
	{
		first_time = false;
		ImGui::DockBuilderRemoveNode(dockspace_id); // clear any previous layout
		ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

		// Split the dockspace: right for Inspector, left for Viewport
		ImGuiID dock_main_id = dockspace_id;
		ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

		ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
		ImGui::DockBuilderDockWindow("Inspector", dock_id_right);

		ImGui::DockBuilderFinish(dockspace_id);
	}

	// --- Panels ---
	ImGui::Begin("Viewport");
	ImGui::Text("Render texture output here");
	ImGui::End();

	ImGui::Begin("Inspector");
	ImGui::Text("Component data here");
	ImGui::End();

	ImGui::End(); // End MainDockSpace

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	// Add these lines for multi-viewport support!
	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
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
