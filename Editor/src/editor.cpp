#include "editor.h"
#include <cstdio>
#include <iostream>
#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_impl_sdl3.h"
#include "../deps/imgui/imgui_impl_opengl3.h"

#include "../deps/imgui/imgui_internal.h"
#include "druid.h"

// Allocate the storage here
Application* editor = nullptr;


u32 viewportFBO      = 0;
u32 viewportTexture  = 0;
u32 viewportWidth    = 0;
u32 viewportHeight   = 0;
u32 depthRB          = 0;

Mesh* skyboxMesh     = nullptr;
u32   cubeMapTexture = 0;
u32   skyboxShader   = 0;
u32   skyboxViewLoc  = 0;
u32   skyboxProjLoc  = 0;


Camera sceneCam = {0};  
u32 entitySizeCache = 0;
u32 entityCount = 0;
InspectorState CurrentInspectorState = EMPTY_VIEW; //set inital inspector view to be empty

u32 inspectorEntityID = 0; //holds the index for the inspector to load component data  

// Helper that (re)creates the framebuffer and attached texture when the viewport size changes
//recreates viewport framebuffer when size changes
static void resizeViewportFramebuffer(int width, int height)
{
    if (width <= 0 || height <= 0) return;                   // Ignore invalid sizes
    if (width == viewportWidth && height == viewportHeight)  // No resize needed
        return;

    viewportWidth  = width;
    viewportHeight = height;

    if (viewportTexture == 0)
        glGenTextures(1, &viewportTexture);

    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportWidth, viewportHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (viewportFBO == 0)
        glGenFramebuffers(1, &viewportFBO);

    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportTexture, 0);

    if (depthRB == 0) glGenRenderbuffers(1, &depthRB);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRB);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewportWidth, viewportHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, depthRB);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[Viewport] Framebuffer incomplete: 0x" << std::hex << status << std::dec << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void renderGameScene()
{
    //render the 3d scene to the off-screen framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
    glViewport(0, 0, viewportWidth, viewportHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //skybox
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(skyboxShader);

    Mat4 sbView = getView(&sceneCam, true);
    glUniformMatrix4fv(skyboxViewLoc, 1, GL_FALSE, &sbView.m[0][0]);
    glUniformMatrix4fv(skyboxProjLoc, 1, GL_FALSE, &sceneCam.projection.m[0][0]);

    glBindVertexArray(skyboxMesh->vao);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);


    glUseProgram(cubeShader);
    Transform newTransform = {0};
    //draw each scene entity
    for(u32 id = 0; id < entitySizeCache; id++)
    {
        if(!isActive[id]) continue;

        //get transform ready
        newTransform = {
            positions[id],
            rotations[id],
            scales[id]
        };
        
        //printf("pos: %f,%f,%f \n",positions[id].x,positions[id].y,positions[id].z);

        //printf("rot: %f,%f,%f,%f \n",rotations[id].x,rotations[id].y,rotations[id].z,rotations[id].w);
    
        //printf("scale: %f,%f,%f \n",scales[id].x,scales[id].y,scales[id].z);
        //update mvp
        updateShaderMVP(cubeShader, newTransform, sceneCam);

        //draw the mesh
        draw(cubeMesh);


    }




    


    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void drawViewportWindow()
{
    ImGui::Begin("Viewport");
    ImVec2 avail = ImGui::GetContentRegionAvail();

    const f32 targetAspect = 16.0f / 9.0f;
    f32 targetW = avail.x;
    f32 targetH = avail.x / targetAspect;
    if (targetH > avail.y)
    {
        targetH = avail.y;
        targetW = targetH * targetAspect;
    }

    resizeViewportFramebuffer((int)targetW, (int)targetH);

    ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - targetW) * 0.5f,
                               cursor.y + (avail.y - targetH) * 0.5f));

    //update camera projection for this aspect ratio
    sceneCam.projection = mat4Perspective(radians(70.0f), targetAspect, 0.1f, 100.0f);

    renderGameScene();

    ImGui::Image((void*)(intptr_t)viewportTexture, ImVec2(targetW, targetH), ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
}

static void drawDebugWindow()
{
    ImGui::Begin("Debug");
    ImGui::Text("FPS %lf", editor->fps);
    ImGui::End();
}

static void drawPrefabsWindow()
{
    ImGui::Begin("Prefabs");
    ImGui::Text("(Archetype designer coming soon)");
    ImGui::End();
}

static void drawSceneListWindow()
{
    ImGui::Begin("Scene List");
    ImGui::InputInt("Max Entity Size", &entitySize);

    if(ImGui::Button("Add Entity"))
    {
        isActive[entityCount] = true;
        scales[entityCount] = {1,1,1};
        positions[entityCount] = {0,0,0};
        rotations[entityCount] = quatIdentity();

        entityCount++;
        
        printf("Added Entity %d\n",entityCount);
    }

    char entityString[16]; //16 chars for saftey
    for(int i = 0; i < entityCount; i++)
    {
        sprintf(entityString, "Entity %d",i);
        //draw list of entities
        if(ImGui::Button(entityString))
        {
            //load details tp Inspector
            printf("%s inspector request\n",entityString);
            //tell the inspector what to read
            CurrentInspectorState = ENTITY_VIEW;
            inspectorEntityID = i;
        
        
        }
    }
    ImGui::End();
}

Vec3 EulerAngles = v3Zero;
static void drawInspectorWindow()
{
    ImGui::Begin("Inspector");
    switch (CurrentInspectorState) 
    {
        case InspectorState::ENTITY_VIEW:
            //draw the scene entity basic data
            ImGui::InputFloat3("position",(float*)&positions[inspectorEntityID]);
            ImGui::InputFloat3("rotation",(float*)&EulerAngles);
            ImGui::InputFloat3("scale",(float*)&scales[inspectorEntityID]);

            //set the rotation element
            rotations[inspectorEntityID] = quatFromEuler(EulerAngles);

        break;

        default:
        case InspectorState::EMPTY_VIEW:
            ImGui::Text("Nout to see here");
    }

    ImGui::End();
}

void drawDockspaceAndPanels()
{
    static bool dockspaceOpen = true;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.25f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", &dockspaceOpen, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceID");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    //build default layout once
    static bool first_time = true;
    if (first_time)
    {
        first_time = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

        ImGuiID dock_id_middle = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);
        ImGuiID dock_id_prefabs = ImGui::DockBuilderSplitNode(dock_id_middle, ImGuiDir_Up, 0.5f, nullptr, &dock_id_middle);

        ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
        ImGui::DockBuilderDockWindow("Inspector", dock_id_right);
        ImGui::DockBuilderDockWindow("Scene List", dock_id_prefabs);
        ImGui::DockBuilderDockWindow("Prefabs", dock_id_middle);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    //--actual docked windows--
    drawViewportWindow();
    drawDebugWindow();
    drawPrefabsWindow();
    drawSceneListWindow();
    drawInspectorWindow();

    ImGui::End(); // MainDockSpace
}


