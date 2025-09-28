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
    const struct aiScene *scene = aiImportFile(
        filename, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
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
        readMaterial(material, aimat, "../" TEXTURE_FOLDER);

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

        // add mesh to resource manager
        manager->meshBuffer[manager->meshUsed] = meshes[i];

        // add to hash map
        insertMap(&manager->mesheIDs, meshName, &manager->meshUsed);

        // store the mesh index in the model (this is the ResourceManager index)
        model.meshIndices[i] = manager->meshUsed;

        // Map Assimp material index to ResourceManager material index
        u32 assimpMaterialIndex = scene->mMeshes[i]->mMaterialIndex;
        model.materialIndices[i] = materialStartIndex + assimpMaterialIndex;

        DEBUG("Mesh %d: Assimp material %d -> ResourceManager material %d", i,
              assimpMaterialIndex, model.materialIndices[i]);

        manager->meshUsed++;
    }

    // Add model to resource manager
    manager->modelBuffer[manager->modelUsed] = model;

    // assign the material to the newly added material
    manager->modelBuffer[manager->modelUsed].materialIndices;
    // add to hash map
    insertMap(&manager->modelIDs, fileName, &manager->modelUsed);

    manager->modelUsed++;

    // Clean up temporary mesh array
    free(meshes);
    aiReleaseImport(scene);
}

void draw(Model* model)
{
 
    for (u32 i = 0; i < model->meshCount; i++)
    {
        u32 meshIndex = model->meshIndices[i];
        u32 materialIndex = model->materialIndices[i];

        // Check bounds
        if (meshIndex >= resources->meshUsed ||
            materialIndex >= resources->materialUsed)
        {
            ERROR("Invalid mesh or material index: mesh=%d/%d, material=%d/%d",
                  meshIndex, resources->meshUsed, materialIndex,
                  resources->materialUsed);
            continue;
        }

        // Update material
        updateMaterial(&resources->materialBuffer[materialIndex]);

        // Draw mesh
        drawMesh(&resources->meshBuffer[meshIndex]);
    }
}
