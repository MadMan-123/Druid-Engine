#include "druid.h"
#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <math.h>
#include <time.h>

#define MAX_NAME_SIZE 64
void loadModelFromAssimp(ResourceManager *manager, const char *filename)
{
    // null check the resource
    if (manager == NULL)
    {
        ERROR("Resource Manager is NULL");
        return;
    }

    const char *fileName = strrchr(filename, '/');
    // remove the first slash
    fileName = fileName ? fileName + 1 : filename;

    // load the model from file
    const struct aiScene *scene = aiImportFile(filename,
         aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality | aiProcess_SortByPType |
        aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals);

    // null check
    if (!scene)
    {
        ERROR("Failed to load model %s: %s\n", filename, aiGetErrorString());
        return;
    }

    // check if there are any meshes
    if (scene->mNumMeshes == 0)
    {
        ERROR("Model %s has no meshes\n", filename);
        aiReleaseImport(scene);
        return;
    }

    // Check if we have enough space in the resource manager
    if (manager->meshUsed + scene->mNumMeshes > manager->meshCount)
    {
        ERROR("Not enough space in mesh buffer for model %s\n", filename);
        aiReleaseImport(scene);
        return;
    }

    if (manager->materialUsed + scene->mNumMaterials > manager->materialCount)
    {
        ERROR("Not enough space in material buffer for model %s\n", filename);
        aiReleaseImport(scene);
        return;
    }
    
    if (manager->modelUsed + 1 > manager->modelCount) {
        ERROR("Not enough space in model buffer for model %s\n", filename);
        aiReleaseImport(scene);
        return;
    }

    // Store the starting indices for this model's resources
    u32 materialStartIndex = manager->materialUsed;

    // Load materials first and store them in ResourceManager
    for (u32 i = 0; i < scene->mNumMaterials; i++)
    {
        // Get the Assimp material
        struct aiMaterial *aimat = scene->mMaterials[i];

        // Read material properties and store in ResourceManager
        Material *material = &manager->materialBuffer[manager->materialUsed];
        *material = (Material){0};
        readMaterial(material, aimat);

        //if the material has no shader assigned, assign a default one
        if (material->shaderHandle == 0 && manager->shaderUsed > 0)
        {   
            u32 defaultShaderIndex = 0;
            if(!findInMap(&manager->shaderIDs, "default", &defaultShaderIndex)) 
            {
                WARN("Default shader not found in shader map. Using first shader in resource manager.");
                material->shaderHandle = manager->shaderHandles[0];
            } 
            else
            {
                //set the shader handle to the default shader
                material->shaderHandle = manager->shaderHandles[defaultShaderIndex];
            }
            WARN("Material %d in model %s has no shader assigned. Using default shader", i, fileName);
        }

        material->uniforms = getMaterialUniforms(material->shaderHandle);

        // Add material to hash map with unique name
        char matName[MAX_NAME_SIZE];
        snprintf(matName, MAX_NAME_SIZE, "%s-material-%d", fileName, i);
        insertMap(&manager->materialIDs, matName, &manager->materialUsed);

        TRACE(
            "Material %d added to resource manager at index %d with name %s\n",
            i, manager->materialUsed, matName);
        
        
        manager->materialUsed++;
    }

    // Create a temporary Model struct on the stack
    Model model;

    model.meshIndices = (u32 *)malloc(sizeof(u32) * scene->mNumMeshes);
    model.materialIndices = (u32 *)malloc(sizeof(u32) * scene->mNumMeshes);
    model.meshCount = scene->mNumMeshes;
    model.materialCount = scene->mNumMaterials;
    model.name = (char *)malloc(sizeof(char) * MAX_NAME_SIZE);
    strncpy(model.name, filename, MAX_NAME_SIZE - 1);
    model.name[MAX_NAME_SIZE - 1] = '\0'; // ensure null termination

    u32 meshCount = 0;
    // load meshes to the resource manager
    Mesh *meshes = loadMeshFromAssimpScene(scene, &meshCount);

    if (meshes == NULL)
    {
        ERROR("Mesh buffer failed to create");
        free(model.meshIndices);
        free(model.materialIndices);
        free(model.name);
        aiReleaseImport(scene);
        return;
    }

    // Store meshes in ResourceManager and create proper index mapping
    for (u32 i = 0; i < scene->mNumMeshes; i++)
    {
        // format the mesh name
        char meshName[MAX_NAME_SIZE];
        snprintf(meshName, MAX_NAME_SIZE, "%s-mesh-%d", fileName, i);

        // store the mesh index in the model BEFORE incrementing meshUsed
        model.meshIndices[i] = manager->meshUsed;

        // add mesh to resource manager
        manager->meshBuffer[manager->meshUsed] = meshes[i];

        // add to hash map
        insertMap(&manager->mesheIDs, meshName, &manager->meshUsed);

        // Map Assimp material index to ResourceManager material index
        u32 assimpMaterialIndex = scene->mMeshes[i]->mMaterialIndex;
        model.materialIndices[i] = materialStartIndex + assimpMaterialIndex;

        DEBUG("Mesh %d: Assimp material %d -> ResourceManager material %d", i,
              assimpMaterialIndex, model.materialIndices[i]);

        manager->meshUsed++;
    }

    // Add model to resource manager
    manager->modelBuffer[manager->modelUsed] = model;

    // add to hash map
    insertMap(&manager->modelIDs, fileName, &manager->modelUsed);

    manager->modelUsed++;

    // Clean up temporary mesh array
    free(meshes);
    aiReleaseImport(scene);
}

void draw(Model *model, u32 shader)
{
 
    for (u32 i = 0; i < model->meshCount; i++)
    {
        u32 meshIndex = model->meshIndices[i];
        u32 materialIndex = model->materialIndices[i];

        // Check bounds
        if (meshIndex >= resources->meshUsed)
        {
            ERROR("Invalid mesh index: mesh=%d/%d", meshIndex, resources->meshUsed);
            continue;
        }

        if (materialIndex >= resources->materialUsed)
        {
            ERROR("Invalid material index: material=%d/%d", materialIndex, resources->materialUsed);
            continue;
        }

        // Update material
        Material *material = &resources->materialBuffer[materialIndex];

        //check if the material is valid
        if (material == NULL) {
            ERROR("Material at index %d is NULL", materialIndex);
            continue;
        }

        // Uniforms are now pre-cached in the material
        updateMaterial(material, &material->uniforms);

        // Check mesh validity before drawing
        Mesh* mesh = &resources->meshBuffer[meshIndex];
        if (mesh && mesh->vao != 0)
        {
            drawMesh(mesh);
        }
        else
        {
            ERROR("Invalid mesh at index %d in model rendering (vao=%d)", meshIndex, mesh ? mesh->vao : 0);
        }
    }
}