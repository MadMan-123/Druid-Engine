#include "../../../include/druid.h"
#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <math.h>
#include <time.h>

void loadModelFromAssimp(ResourceManager *manager, const c8 *filename)
{
    if (manager == NULL)
    {
        ERROR("Resource Manager is NULL");
        return;
    }

    const c8 *baseName = strrchr(filename, '/');
    baseName = baseName ? baseName + 1 : filename;

    // pad to MAX_NAME_SIZE for insertMap
    c8 fileName[MAX_NAME_SIZE];
    memset(fileName, 0, MAX_NAME_SIZE);
    strncpy(fileName, baseName, MAX_NAME_SIZE - 1);

    const struct aiScene *scene = aiImportFile(filename,
         aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality | aiProcess_SortByPType |
        aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals);

    if (!scene)
    {
        ERROR("Failed to load model %s: %s\n", filename, aiGetErrorString());
        return;
    }

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

    for (u32 i = 0; i < scene->mNumMaterials; i++)
    {
        struct aiMaterial *aimat = scene->mMaterials[i];

        Material *material = &manager->materialBuffer[manager->materialUsed];
        *material = (Material){0};
        readMaterial(material, aimat);

        // Add material to hash map with unique name
        c8 matName[MAX_NAME_SIZE];
        snprintf(matName, MAX_NAME_SIZE, "%s-material-%d", fileName, i);
        insertMap(&manager->materialIDs, matName, &manager->materialUsed);

        if(DEBUG_RESOURCES)
        TRACE(
            "Material %d added to resource manager at index %d with name %s\n",
            i, manager->materialUsed, matName);
        
        
        manager->materialUsed++;
    }

    // Create a temporary Model struct on the stack
    Model model;

    model.meshIndices = (u32 *)dalloc(sizeof(u32) * scene->mNumMeshes, MEM_TAG_MODEL);
    model.materialIndices = (u32 *)dalloc(sizeof(u32) * scene->mNumMeshes, MEM_TAG_MODEL);
    model.meshCount = scene->mNumMeshes;
    model.materialCount = scene->mNumMaterials;
    model.name = (c8 *)dalloc(sizeof(c8) * MAX_NAME_SIZE, MEM_TAG_MODEL);
    strncpy(model.name, filename, MAX_NAME_SIZE - 1);
    model.name[MAX_NAME_SIZE - 1] = '\0'; // ensure null termination

    u32 meshCount = 0;
    Mesh *meshes = loadMeshFromAssimpScene(scene, &meshCount);

    if (meshes == NULL)
    {
        ERROR("Mesh buffer failed to create");
        dfree(model.meshIndices, sizeof(u32) * scene->mNumMeshes, MEM_TAG_MODEL);
        dfree(model.materialIndices, sizeof(u32) * scene->mNumMeshes, MEM_TAG_MODEL);
        dfree(model.name, sizeof(c8) * MAX_NAME_SIZE, MEM_TAG_MODEL);
        aiReleaseImport(scene);
        return;
    }

    // Also compute the bounding radius across all meshes for frustum culling.
    f32 maxRadiusSq = 0.0f;
    for (u32 i = 0; i < scene->mNumMeshes; i++)
    {
        // format the mesh name
        c8 meshName[MAX_NAME_SIZE];
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

        if(DEBUG_RESOURCES)
        DEBUG("Mesh %d: Assimp material %d -> ResourceManager material %d", i,
              assimpMaterialIndex, model.materialIndices[i]);

        // Compute bounding radius from Assimp vertex data
        struct aiMesh *am = scene->mMeshes[i];
        for (u32 v = 0; v < am->mNumVertices; v++)
        {
            f32 x = am->mVertices[v].x, y = am->mVertices[v].y, z = am->mVertices[v].z;
            f32 d2 = x*x + y*y + z*z;
            if (d2 > maxRadiusSq) maxRadiusSq = d2;
        }

        manager->meshUsed++;
    }
    model.boundingRadius = sqrtf(maxRadiusSq);

    // Add model to resource manager
    manager->modelBuffer[manager->modelUsed] = model;

    // add to hash map
    insertMap(&manager->modelIDs, fileName, &manager->modelUsed);

    manager->modelUsed++;

    // Clean up temporary mesh array
    dfree(meshes, sizeof(Mesh) * meshCount, MEM_TAG_MESH);
    aiReleaseImport(scene);
}

void resRegisterPrimitive(ResourceManager *manager, const c8 *name, Mesh *mesh)
{
    if (!manager || !name || !mesh) return;
    if (manager->meshUsed >= manager->meshCount ||
        manager->modelUsed >= manager->modelCount)
    {
        ERROR("resRegisterPrimitive: no space for '%s'", name);
        return;
    }

    u32 meshIdx = manager->meshUsed;
    manager->meshBuffer[meshIdx] = *mesh;
    insertMap(&manager->mesheIDs, name, &manager->meshUsed);
    manager->meshUsed++;

    // build model with single mesh, no material
    Model model;
    model.meshIndices = (u32 *)dalloc(sizeof(u32), MEM_TAG_MODEL);
    model.materialIndices = (u32 *)dalloc(sizeof(u32), MEM_TAG_MODEL);
    model.meshIndices[0] = meshIdx;
    model.materialIndices[0] = 0;
    model.meshCount = 1;
    model.materialCount = 0;
    model.boundingRadius = 1.0f;  // primitives are unit-scale (sphere r=1, box half-ext=1)

    u32 len = (u32)strlen(name);
    model.name = (c8 *)dalloc(len + 1, MEM_TAG_MODEL);
    memcpy(model.name, name, len + 1);

    manager->modelBuffer[manager->modelUsed] = model;
    insertMap(&manager->modelIDs, name, &manager->modelUsed);
    manager->modelUsed++;

    INFO("Registered primitive model '%s'", name);
}

void draw(Model *model, u32 shader, b8 shouldUpdateMaterials)
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

        if (materialIndex >= resources->materialUsed && shouldUpdateMaterials)
        {
            ERROR("Invalid material index: material=%d/%d", materialIndex, resources->materialUsed);
            continue;
        }

        // Update material
        Material *material = &resources->materialBuffer[materialIndex];

        //check if the material is valid
        if (material == NULL && shouldUpdateMaterials) {
            ERROR("Material at index %d is NULL", materialIndex);
            continue;
        }

        if(shouldUpdateMaterials)
        {
            // Determine uniforms for the currently bound shader (shader passed to draw())
            MaterialUniforms uniforms = getMaterialUniforms(shader);
            updateMaterial(material, &uniforms);
        }
        // Check mesh validity before drawing
        Mesh* mesh = &resources->meshBuffer[meshIndex];
        if (mesh)
        {
            drawMesh(mesh);
        }
        else
        {
            ERROR("Invalid mesh at index %d in model rendering (vao=%d)", meshIndex, mesh ? mesh->vao : 0);
        }
    }
}