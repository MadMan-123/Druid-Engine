
#include "editor.h"
#include "../deps/imgui/imgui.h"
#include "../deps/imgui/imgui_impl_opengl3.h"
#include "../deps/imgui/imgui_impl_sdl3.h"
#include <cstdio>
#include <cstring>
#include <iostream>

#include "../deps/imgui/imgui_internal.h"
#include "entitypicker.h"

// buffer of 2D array of strings
const char **consoleLines = NULL;

// Allocate the storage here
Application *editor = nullptr;

// Scene manager instance (defined here so UI can reference it)
SceneManager* sceneManager = NULL;

// UI state for scene menu modals
static char scenePathBuffer[512] = "";
static b8 showSaveModal = false;
static b8 showLoadModal = false;
static b8 showNewSceneModal = false;

// Multi-FBO system
Framebuffer viewportFBs[MAX_FBOS] = {0};
Framebuffer finalDisplayFB = {0};  // Final processed result for ImGui
u32 viewportWidth = 0;
u32 viewportHeight = 0;
u32 activeFBO = 0;  // Which FBO to render to

// Screen quad for FBO rendering
Mesh *screenQuadMesh = nullptr;
Mesh *skyboxMesh = nullptr;
u32 cubeMapTexture = 0;
u32 skyboxShader = 0;
u32 skyboxViewLoc = 0;
u32 skyboxProjLoc = 0;

f32 viewportWidthPixels = 0.0f;
f32 viewportHeightPixels = 0.0f;
f32 viewportOffsetX = 0.0f;
f32 viewportOffsetY = 0.0f;

Camera sceneCam = {0};
u32 entitySizeCache = 0;
Vec3 EulerAngles = v3Zero;

b8 manipulateTransform = false;

// entity data
u32 entityCount = 0;
InspectorState currentInspectorState =
    EMPTY_VIEW; // set inital inspector view to be empty

u32 inspectorEntityID =
    0; // holds the index for the inspector to load component data

u32 arrowShader = 0;
u32 colourLocation = 0;
u32 fboShader = 0;  // FBO post-processing shader

ManipulateTransformState manipulateState = MANIPULATE_POSITION;


static void drawTextureSelector(const char *label, u32 *textureHandle, const char *comboID)
{
    if (!textureHandle || !resources) return;
    ImGui::Text("%s", label);
    ImGui::Image((void *)(intptr_t)(*textureHandle), ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0));
    ImGui::SameLine();
    if (ImGui::BeginCombo(comboID, "Select Texture"))
    {
        for (u32 texIdx = 0; texIdx < resources->textureIDs.capacity; texIdx++)
        {
            if (resources->textureIDs.pairs[texIdx].occupied)
            {
                const char *texName = (const char *)resources->textureIDs.pairs[texIdx].key;
                u32 textureIndex = *(u32 *)resources->textureIDs.pairs[texIdx].value;
                u32 handle = resources->textureHandles[textureIndex];

                const b8 is_selected = (*textureHandle == handle);
                if (ImGui::Selectable(texName, is_selected))
                {
                    *textureHandle = handle;
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

static void resizeViewportFramebuffers(u32 width, u32 height)
{
    if (width <= 0 || height <= 0)
        return; // Ignore invalid sizes
    if (width == viewportWidth && height == viewportHeight) // No resize needed
        return;

    viewportWidth = width;
    viewportHeight = height;

    // Resize all FBOs
    for (u32 i = 0; i < MAX_FBOS; i++)
    {
        if (viewportFBs[i].fbo == 0)
        {
            viewportFBs[i] = createFramebuffer(viewportWidth, viewportHeight, GL_RGBA8, true);
        }
        else
        {
            resizeFramebuffer(&viewportFBs[i], viewportWidth, viewportHeight);
        }
    }
    
    // Resize final display FBO
    if (finalDisplayFB.fbo == 0)
    {
        finalDisplayFB = createFramebuffer(viewportWidth, viewportHeight, GL_RGBA8, false);
    }
    else
    {
        resizeFramebuffer(&finalDisplayFB, viewportWidth, viewportHeight);
    }
}

static void renderGameScene()
{
    // Ensure framebuffer exists before rendering
    if (viewportFBs[activeFBO].fbo == 0 || viewportWidth == 0 || viewportHeight == 0)
    {
        // Framebuffer not initialized yet, skip rendering
        return;
    }

    // Update core shader UBO (time + viewProj) once per frame
    {
        f32 t = (f32)ImGui::GetTime();
        updateCoreShaderUBO(t, &sceneCam.pos);
    }

    bindFramebuffer(&viewportFBs[activeFBO]);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.2f, 0.4f, 0.8f, 1.0f); // Blue clear color to test scene rendering
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // skybox
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(skyboxShader);

    Mat4 sbView = getView(&sceneCam, true);
    glUniformMatrix4fv(skyboxViewLoc, 1, GL_FALSE, &sbView.m[0][0]);
    glUniformMatrix4fv(skyboxProjLoc, 1, GL_FALSE,
                       &sceneCam.projection.m[0][0]);

    glBindVertexArray(skyboxMesh->vao);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // draw each scene entity
    Transform newTransform = {0};
    for (u32 id = 0; id < entitySizeCache; id++)
    {
        if (!isActive[id])
            continue;

        // get transform ready
        newTransform = {positions[id], rotations[id], scales[id]};

        // Get the mesh name for this specific entity
      
        // draw the model
        u32 modelID = modelIDs[id];
        if (modelID == (u32)-1)
        {
            // Entity has no model assigned, skip rendering
            continue;
        }
        if (modelID < resources->modelUsed)
        {
            Model* model = &resources->modelBuffer[modelID];
            if (!model)
            {
                ERROR("Model pointer is NULL for entity %d, modelID %d", id, modelID);
                continue;
            }
            
            for (u32 i = 0; i < model->meshCount; i++)
            {
                u32 meshIndex = model->meshIndices[i];
                
                // Check mesh index bounds
                if (meshIndex >= resources->meshUsed)
                {
                    ERROR("Invalid mesh index in editor rendering: entity=%d, model=%d, mesh=%d/%d", id, modelID, meshIndex, resources->meshUsed);
                    continue;
                }
                
                Mesh* mesh = &resources->meshBuffer[meshIndex];
                if (!mesh || mesh->vao == 0)
                {
                    ERROR("Invalid mesh in editor rendering: entity=%d, model=%d, meshIndex=%d (vao=%d)", id, modelID, meshIndex, mesh ? mesh->vao : 0);
                    continue;
                }
                
                // Prefer per-entity material override if set, otherwise use model's material
                u32 materialIndex = model->materialIndices[i];
                if (entityMaterialIDs && entityMaterialIDs[id] != (u32)-1) 
                {
                    materialIndex = entityMaterialIDs[id];
                }
                if (materialIndex >= resources->materialUsed) 
                {
                    ERROR("Invalid material index in editor rendering: entity=%d, material=%d/%d", id, materialIndex, resources->materialUsed);
                    continue;
                }
                Material* material = &resources->materialBuffer[materialIndex];

                // Prefer per-entity shader handle if set, otherwise use global default shader
                u32 entityShader = shaderHandles[id];
                u32 shaderToUse = (entityShader != 0) ? entityShader : shader;
                glUseProgram(shaderToUse);
                updateShaderMVP(shaderToUse, newTransform, sceneCam);
                MaterialUniforms uniforms = getMaterialUniforms(shaderToUse);
                updateMaterial(material, &uniforms);
                drawMesh(mesh);
            }
        }
        else
        {
            ERROR("Invalid model ID for entity %d: modelID=%d, modelUsed=%d", id, modelID, resources->modelUsed);
        }
    }

    if (manipulateTransform)
    {
        Vec3 pos = positions[inspectorEntityID];
        const f32 scaleSize = 0.1f;
        const f32 scaleLength = 1.1f;

        Transform X = {v3Add(pos, v3Right), quatIdentity(),
                       {scaleLength, scaleSize, scaleSize}};
        Transform Y = {v3Add(pos, v3Up), quatIdentity(),
                       {scaleSize, scaleLength, scaleSize}};

        Transform Z = {v3Add(pos, v3Back), quatIdentity(),
                       {scaleSize, scaleSize, scaleLength}};

        // visually draw the arrows

        // *** Use the arrow shader ONLY for the arrows ***
        glUseProgram(arrowShader);

        updateShaderMVP(arrowShader, X, sceneCam);
        glUniform3f(colourLocation, 0.0f, 1.0f, 0.0f);
        drawMesh(cubeMesh);
        updateShaderMVP(arrowShader, Y, sceneCam);
        glUniform3f(colourLocation, 1.0f, 0.0f, 0.0f);
        drawMesh(cubeMesh);
        updateShaderMVP(arrowShader, Z, sceneCam);
        glUniform3f(colourLocation, 0.0f, 0.0f, 1.0f);
        drawMesh(cubeMesh);
    }

    // Ensure we're in a clean OpenGL state before unbinding
    unbindFramebuffer();

    // Reset OpenGL state to defaults
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
}

ImVec2 g_viewportScreenPos = ImVec2(0, 0);
ImVec2 g_viewportSize = ImVec2(0, 0);

static void drawViewportWindow()
{
    ImGui::Begin("Viewport");

    ImVec2 viewportWindowPos =
        ImGui::GetWindowPos(); // screen position of the window
    ImVec2 avail = ImGui::GetContentRegionAvail();
    canMoveViewPort =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    const f32 targetAspect = 16.0f / 9.0f;
    f32 targetW = avail.x;
    f32 targetH = avail.x / targetAspect;
    if (targetH > avail.y)
    {
        targetH = avail.y;
        targetW = targetH * targetAspect;
    }

    resizeViewportFramebuffers((int)targetW, (int)targetH);
    // Keep the ID framebuffer sized to the viewport so picking reads use correct coords
    resizeIDFramebuffer((int)targetW, (int)targetH);

    ImVec2 cursor = ImGui::GetCursorPos();
    ImVec2 imageOffset =
        ImVec2((avail.x - targetW) * 0.5f, (avail.y - targetH) * 0.5f);
    ImGui::SetCursorPos(
        ImVec2(cursor.x + imageOffset.x, cursor.y + imageOffset.y));

    // Save image position for mouse picking
    g_viewportScreenPos = ImVec2(viewportWindowPos.x + imageOffset.x,
                                 viewportWindowPos.y + imageOffset.y);
    g_viewportSize = ImVec2(targetW, targetH);

    // update camera projection
    sceneCam.projection =
        mat4Perspective(radians(70.0f), targetAspect, 0.1f, 100.0f);

    renderIDPass();
    renderGameScene();

    // Debug: Show what's happening with the conditions
    bool shaderReady = (fboShader != 0);
    bool finalFBOReady = (finalDisplayFB.fbo != 0);  
    bool activeFBOReady = (viewportFBs[activeFBO].fbo != 0);
    
    // Post-processing pipeline: render active FBO through shader to final display FBO
    if (shaderReady && finalFBOReady && activeFBOReady) {
        // Bind final display FBO as render target
        bindFramebuffer(&finalDisplayFB);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Render active FBO through post-processing shader
        renderFBOToScreen(activeFBO, fboShader);
        
        // Unbind FBO
        unbindFramebuffer();
        
        // Display the processed result
        ImGui::Image((void *)(intptr_t)finalDisplayFB.texture, ImVec2(targetW, targetH),
                     ImVec2(0, 1), ImVec2(1, 0));
    } else {
        // Fallback to raw texture if available
        if (activeFBOReady) {
            ImGui::Image((void *)(intptr_t)viewportFBs[activeFBO].texture, ImVec2(targetW, targetH),
                         ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::Text("FBOs not ready - Viewport: %dx%d", viewportWidth, viewportHeight);
        }
    }

    ImGui::End();

    // Debug prints
    // DEBUG("Viewport Image Pos: (%.2f, %.2f)\n", g_viewportScreenPos.x,
    // g_viewportScreenPos.y); DEBUG("Viewport Image Size: (%.2f x %.2f)\n",
    // g_viewportSize.x, g_viewportSize.y);
}

static void drawDebugWindow()
{
    const char *inspectorStateNames[] = {"EMPTY_VIEW", "ENTITY_VIEW"};
    ImGui::Begin("Debug");
    ImGui::Text("FPS %lf", editor->fps);
    ImGui::Text("Entity Count: %d", entityCount);
    ImGui::Text("Entity Size: %d", entitySize);
    ImGui::Text("Inspector Entity ID: %d", inspectorEntityID);
    ImGui::Text("Inspector State: %d",
                inspectorStateNames[currentInspectorState]);
    ImGui::Text("Viewport Size: %.0f x %.0f", viewportWidth, viewportHeight);
    
    // Multi-FBO System Info
    ImGui::Separator();
    ImGui::Text("Multi-FBO System");
    ImGui::Text("Active FBO: %d (ID: %d)", activeFBO, viewportFBs[activeFBO].fbo);
    ImGui::Text("Final Display FBO: ID %d", finalDisplayFB.fbo);
    ImGui::Text("FBO Shader: ID %d", fboShader);
    ImGui::Text("Screen Quad: %s", screenQuadMesh ? "Created" : "NULL");
    ImGui::Text("FBOs Created: %d/%d", 
               (viewportFBs[0].fbo != 0) + (viewportFBs[1].fbo != 0) + (viewportFBs[2].fbo != 0) + (viewportFBs[3].fbo != 0),
               MAX_FBOS);
    
    // Scene rendering debug info
    ImGui::Separator();
    ImGui::Text("Scene Content");
    ImGui::Text("Skybox Mesh: %s (VAO: %d)", skyboxMesh ? "Loaded" : "NULL", skyboxMesh ? skyboxMesh->vao : 0);
    ImGui::Text("Skybox Shader: %d", skyboxShader);
    ImGui::Text("Entities: %d/%d active", entityCount, entitySizeCache);
    if (entityCount > 0) {
        ImGui::Text("First entity model ID: %d", modelIDs[0]);
    }
    // input axis
    ImGui::Text("Input Axis: (%.2f, %.2f)", xInputAxis, yInputAxis);
    ImGui::Text("Mouse Position: (%.2f, %.2f)", ImGui::GetMousePos().x,
                ImGui::GetMousePos().y);

    if (resources)
    {
        // resource manager info
        ImGui::Text("Resources:");
        ImGui::Text("Meshes: %d : %d", resources->meshUsed,
                    resources->meshCount);
        ImGui::Text("Textures: %d : %d", resources->textureUsed,
                    resources->textureCount);
        ImGui::Text("Shaders: %d : %d", resources->shaderUsed,
                    resources->shaderCount);
        ImGui::Text("Materials: %d : %d", resources->materialUsed,
                    resources->materialCount);
        ImGui::Text("Models: %d : %d", resources->modelUsed,
                    resources->modelCount);
    }

    // draw console line
    u32 dummy = -1; // No selection
    // ImGui::ListBox("Console", &dummy, consoleLines, MAX_CONSOLE_LINES, 10);
    ImGui::End();
}

static void drawPrefabsWindow()
{
    ImGui::Begin("Prefabs");
    ImGui::Text("(Archetype designer coming soon)");
    ImGui::End();
}
static Vec3 EulerAnglesDegrees = v3Zero;

static void drawSceneListWindow()
{
    ImGui::Begin("Scene List");
    ImGui::InputInt("Max Entity Size", &entitySize);

    if (ImGui::Button("Add Entity"))
    {
        // Add bounds checking for entity creation
        if (entityCount >= entitySizeCache)
        {
            WARN("Cannot add entity: reached maximum entity count (%u)", entitySizeCache);
        }
        else 
        {
            isActive[entityCount] = true;
            scales[entityCount] = {1, 1, 1};
            positions[entityCount] = {0, 0, 0};
            rotations[entityCount] = quatIdentity();
            modelIDs[entityCount] = (u32)-1; // Initialize modelID to an invalid index
            entityMaterialIDs[entityCount] = (u32)-1; // no custom material
            shaderHandles[entityCount] = 0; // no custom shader

            // make the inital name this
            sprintf(&names[entityCount * MAX_NAME_SIZE], "Entity %d", entityCount);

            entityCount++;
            DEBUG("Added Entity %d\n", entityCount);
        }
    }

    char *entityName;
    for (u32 i = 0; i < entityCount; i++)
    {
        entityName = &names[i * MAX_NAME_SIZE];

        ImGui::PushID(i);
        const char *button_label =
            (entityName[0] == '\0') ? "[Unnamed Entity]" : entityName;

        // draw list of entities
        if (ImGui::Button(button_label))
        {
            // tell the inspector what to read
            currentInspectorState = ENTITY_VIEW;
            inspectorEntityID = i;

            Vec3 eulerRadians = eulerFromQuat(rotations[inspectorEntityID]);
            EulerAnglesDegrees.x = degrees(eulerRadians.x);
            EulerAnglesDegrees.y = degrees(eulerRadians.y);
            EulerAnglesDegrees.z = degrees(eulerRadians.z);
        }
        ImGui::PopID();
    }
    ImGui::End();
}

static void drawInspectorWindow()
{
    ImGui::Begin("Inspector");
    switch (currentInspectorState)
    {
    default:
    case InspectorState::EMPTY_VIEW:
        ImGui::Text("Nout to see here");
        break;
    case InspectorState::ENTITY_VIEW:
        // Add bounds checking for inspector entity access
        if (inspectorEntityID >= entitySizeCache) {
            ImGui::Text("Invalid entity ID: %u (max: %u)", inspectorEntityID, entitySizeCache);
            break;
        }
        
        ImGui::InputText("##Name", &names[inspectorEntityID * MAX_NAME_SIZE],
                         MAX_NAME_SIZE);
        // draw the scene entity basic data
        ImGui::InputFloat3("position", (f32 *)&positions[inspectorEntityID]);

        if (ImGui::InputFloat3("rotation", (f32 *)&EulerAnglesDegrees))
        {
            Vec3 eulerRadians;
            eulerRadians.x = radians(EulerAnglesDegrees.x);
            eulerRadians.y = radians(EulerAnglesDegrees.y);
            eulerRadians.z = radians(EulerAnglesDegrees.z);
            // set the rotation element
            rotations[inspectorEntityID] = quatFromEuler(eulerRadians);
        }
        ImGui::InputFloat3("scale", (f32 *)&scales[inspectorEntityID]);

        u32 currentModelID = modelIDs[inspectorEntityID];
        u32 selectedIndex = (currentModelID < resources->modelUsed) ? currentModelID : 0;
        
        // Show warning if entity has no model assigned
        if (currentModelID == (u32)-1)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ No model assigned to this entity");
        }
        
        if (ImGui::BeginListBox("Models"))
        {
            // Add "None" option for no model
            b8 isNoneSelected = (currentModelID == (u32)-1);
            if(ImGui::Selectable("None", isNoneSelected))
            {
                modelIDs[inspectorEntityID] = (u32)-1;
            }
            
            for(u32 modelIdx = 0; modelIdx < resources->modelUsed; modelIdx++)
            {
                const b8 isSelected = (currentModelID == modelIdx);
                Model *model = &resources->modelBuffer[modelIdx];
                
                // Create unique ID for each model selectable
                ImGui::PushID(modelIdx);
                if(ImGui::Selectable(model->name, isSelected))
                {
                    modelIDs[inspectorEntityID] = modelIdx;
                }
                ImGui::PopID();
            }
        }
        
        


        ImGui::EndListBox();
        
        //list the material data 
        u32 modelID = modelIDs[inspectorEntityID];
        if (modelID < resources->modelUsed)
        {
            Model *model = &resources->modelBuffer[modelID];
            if (model->meshCount > 0)
            {
                // Determine default and effective material indices
                u32 defaultMaterialIndex = model->materialIndices[0];
                u32 effectiveMaterialIndex = defaultMaterialIndex;
                if (entityMaterialIDs && entityMaterialIDs[inspectorEntityID] != (u32)-1) {
                    effectiveMaterialIndex = entityMaterialIDs[inspectorEntityID];
                }
                // guard
                if (effectiveMaterialIndex >= resources->materialUsed) {
                    ERROR("Invalid material index for inspector: %u (used=%u)", effectiveMaterialIndex, resources->materialUsed);
                    break;
                }

                Material *mat = &resources->materialBuffer[effectiveMaterialIndex];

                b8 isCustom = (effectiveMaterialIndex != defaultMaterialIndex);

                ImGui::Text("Material");
                ImGui::SameLine();
                if (isCustom) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "(Custom)");
                    ImGui::SameLine();
                }

                // Create a custom material clone for this entity
                if (ImGui::Button("Make Custom Material"))
                {
                    if (!resources)
                    {
                        WARN("Resource manager not initialized");
                    }
                    else if (resources->materialUsed >= resources->materialCount)
                    {
                        WARN("Cannot create custom material: material buffer full");
                    }
                    else
                    {
                        u32 newIndex = resources->materialUsed;
                        // copy the model's default material into resource manager (so custom starts from default)
                        resources->materialBuffer[newIndex] = resources->materialBuffer[defaultMaterialIndex];

                        // generate a name for the material
                        char newName[256];
                        const char *entityName = &names[inspectorEntityID * MAX_NAME_SIZE];
                        if (entityName == NULL || entityName[0] == '\0')
                            entityName = model->name;
                        snprintf(newName, sizeof(newName), "%s-custom-%u", entityName, newIndex);

                        insertMap(&resources->materialIDs, newName, &resources->materialUsed);
                        resources->materialUsed++;
                        // assign to entity
                        if (entityMaterialIDs)
                            entityMaterialIDs[inspectorEntityID] = newIndex;

                        INFO("Created custom material '%s' at index %u for entity %u", newName, newIndex, inspectorEntityID);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Custom Material"))
                {
                    if (entityMaterialIDs)
                    {
                        entityMaterialIDs[inspectorEntityID] = (u32)-1;
                        INFO("Cleared custom material for entity %u", inspectorEntityID);
                        // recompute effective material to default
                        effectiveMaterialIndex = defaultMaterialIndex;
                        mat = &resources->materialBuffer[effectiveMaterialIndex];
                    }
                }
                // If we just created a custom material above, refresh mat/effective index
                if (entityMaterialIDs && entityMaterialIDs[inspectorEntityID] != (u32)-1) {
                    effectiveMaterialIndex = entityMaterialIDs[inspectorEntityID];
                    if (effectiveMaterialIndex < resources->materialUsed)
                        mat = &resources->materialBuffer[effectiveMaterialIndex];
                }

                drawTextureSelector("Albedo", &mat->albedoTex, "##Select Albedo");
                drawTextureSelector("Normal", &mat->normalTex, "##Select Normal");
                drawTextureSelector("Metallic", &mat->metallicTex, "##Select Metallic");
                drawTextureSelector("Roughness", &mat->roughnessTex, "##Select Roughness");

                ImGui::ColorEdit3("Colour", (f32 *)&mat->colour);
                ImGui::SliderFloat("Metallic", &mat->metallic, 0.0f, 1.0f); 
                ImGui::SliderFloat("Roughness", &mat->roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("Transparency", &mat->transparency, 0.0f, 1.0f);

                // Shader selection
                const char* currentShaderName = "None";
                // Find the name of the current shader
                for (u32 shaderNameIdx = 0; shaderNameIdx < resources->shaderIDs.capacity; shaderNameIdx++) {
                    if (resources->shaderIDs.pairs[shaderNameIdx].occupied) {
                        u32 shaderIndex = *(u32*)resources->shaderIDs.pairs[shaderNameIdx].value;
                        if (resources->shaderHandles[shaderIndex] == shaderHandles[inspectorEntityID]) {
                            currentShaderName = (const char*)resources->shaderIDs.pairs[shaderNameIdx].key;
                            break;
                        }
                    }
                }

                if (ImGui::BeginCombo("Shader", currentShaderName))
                {
                    for (u32 shaderIdx = 0; shaderIdx < resources->shaderIDs.capacity; shaderIdx++)
                    {
                        if (resources->shaderIDs.pairs[shaderIdx].occupied)
                        {
                            const char* shaderName = (const char*)resources->shaderIDs.pairs[shaderIdx].key;
                            u32 shaderIndex = *(u32*)resources->shaderIDs.pairs[shaderIdx].value;
                            u32 shaderHandle = resources->shaderHandles[shaderIndex];

                            const b8 is_selected = (shaderHandles[inspectorEntityID] == shaderHandle);
                            if (ImGui::Selectable(shaderName, is_selected))
                            {
                                shaderHandles[inspectorEntityID] = shaderHandle;
                            }
                            if (is_selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }

        // material
        break;
    }

    ImGui::End();
}



// Initialize all FBOs and screen quad
void initMultiFBOs()
{
    // Create screen quad mesh if not already created
    if (screenQuadMesh == nullptr)
    {
        screenQuadMesh = createQuadMesh();
    }
    
    // Initialize all FBOs as empty - they will be created on first resize
    for (u32 i = 0; i < MAX_FBOS; i++)
    {
        memset(&viewportFBs[i], 0, sizeof(Framebuffer));
    }
    memset(&finalDisplayFB, 0, sizeof(Framebuffer));
    
    // Reset viewport dimensions
    viewportWidth = 0;
    viewportHeight = 0;
    activeFBO = 0;
}

// Cleanup all FBOs and screen quad
void destroyMultiFBOs()
{
    for (u32 i = 0; i < MAX_FBOS; i++)
    {
        if (viewportFBs[i].fbo != 0)
        {
            destroyFramebuffer(&viewportFBs[i]);
            memset(&viewportFBs[i], 0, sizeof(Framebuffer));
        }
    }
    
    if (finalDisplayFB.fbo != 0) 
    {
        destroyFramebuffer(&finalDisplayFB);
        memset(&finalDisplayFB, 0, sizeof(Framebuffer));
    }
    
    if (screenQuadMesh)
    {
        freeMesh(screenQuadMesh);
        screenQuadMesh = nullptr;
    }
}

// Render FBO texture to screen using screen quad
void renderFBOToScreen(u32 fboIndex, u32 shaderProgram)
{
    if (fboIndex >= MAX_FBOS || !screenQuadMesh || viewportFBs[fboIndex].fbo == 0 || shaderProgram == 0) return;
    
    glDisable(GL_DEPTH_TEST);
    glUseProgram(shaderProgram);
    
    // Set the screenTexture uniform
    u32 textureUniform = glGetUniformLocation(shaderProgram, "screenTexture");
    if (textureUniform != (u32)-1)
    {
        glUniform1i(textureUniform, 0);
    }
    
    // Bind texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, viewportFBs[fboIndex].texture);
    
    // Draw screen quad using glDrawArrays since it has no indices
    glBindVertexArray(screenQuadMesh->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    
    glEnable(GL_DEPTH_TEST);
}

void drawDockspaceAndPanels()
{
    static b8 dockspaceOpen = true;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |=
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.25f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", &dockspaceOpen, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceID");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    // Main menu bar (Scene menu)
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Scene"))
        {
            if (ImGui::MenuItem("New Scene"))
            {
                // Optionally show a modal to set name/capacity
                showNewSceneModal = true;
            }
            if (ImGui::MenuItem("Save Scene As..."))
            {
                showSaveModal = true;
            }
            if (ImGui::MenuItem("Load Scene..."))
            {
                showLoadModal = true;
            }
            ImGui::Separator();
            if (sceneManager && sceneManager->sceneCount > 0)
            {
                // List existing scenes for quick switch/remove
                for (u32 i = 0; i < sceneManager->sceneCount; i++)
                {
                    char label[64];
                    snprintf(label, sizeof(label), "Scene %u", i);
                    if (ImGui::MenuItem(label, NULL, (int)(sceneManager->currentScene == i)))
                    {
                        // switch scene
                        switchScene(sceneManager, i);
                    }
                    ImGui::SameLine();
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("Remove"))
                    {
                        removeScene(sceneManager, i);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // build default layout once
    static b8 first_time = true;
    if (first_time)
    {
        first_time = false;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(
            dockspace_id,
            dockspace_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(
            dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

        ImGuiID dock_id_middle = ImGui::DockBuilderSplitNode(
            dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);
        ImGuiID dock_id_prefabs = ImGui::DockBuilderSplitNode(
            dock_id_middle, ImGuiDir_Up, 0.5f, nullptr, &dock_id_middle);

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

    // --- Scene menu modals ---
    if (showSaveModal)
    {
        ImGui::OpenPopup("Save Scene As");
        showSaveModal = false;
    }
    if (ImGui::BeginPopupModal("Save Scene As", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Path", scenePathBuffer, sizeof(scenePathBuffer));
        if (ImGui::Button("Save"))
        {
            if (sceneManager && sceneManager->sceneCount > 0)
            {
                // Save current scene by index
                u32 idx = sceneManager->currentScene;
                saveScene(scenePathBuffer, &sceneManager->scenes[idx]);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (showLoadModal)
    {
        ImGui::OpenPopup("Load Scene");
        showLoadModal = false;
    }
    if (ImGui::BeginPopupModal("Load Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Path", scenePathBuffer, sizeof(scenePathBuffer));
        if (ImGui::Button("Load"))
        {
            SceneMetaData md = loadScene(scenePathBuffer);
            if (sceneManager)
            {
                addScene(sceneManager, &md);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (showNewSceneModal)
    {
        ImGui::OpenPopup("New Scene");
        showNewSceneModal = false;
    }
    if (ImGui::BeginPopupModal("New Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        // For new scene we only need capacity for now
        static int newCapacity = 128;
        ImGui::InputInt("Initial Capacity", &newCapacity);
        if (ImGui::Button("Create"))
        {
            if (!sceneManager)
            {
                sceneManager = createSceneManager((u32)newCapacity);
            }
            else
            {
                // Optionally add an empty SceneMetaData
                SceneMetaData md = {0};
                addScene(sceneManager, &md);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void editorLog(LogLevel level, const char *msg)
{
    // add the message to the console lines to be rendered
    for (u32 i = 0; i < MAX_CONSOLE_LINES; i++)
    {
        if (consoleLines[i] == NULL)
        {
            consoleLines[i] = strdup(msg); // duplicate the message string
            break;
        }
    }
}

