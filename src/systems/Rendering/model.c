#include "druid.h"
#include <math.h>
#include <time.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#define MAX_NAME_SIZE 64
Model* loadModelFromAssimp(const ResourceManager* manager,const char* filename)
{
    //load the model from file
    const struct aiScene* scene = aiImportFile(filename,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType |
        aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals

    );

    //null check
    if (!scene)
    {
        printf("Failed to load model %s: %s\n", filename, aiGetErrorString());
        aiReleaseImport(scene);
        return NULL;
    }

    //check if there are any meshes
    if (scene->mNumMeshes == 0)
    {
        printf("Model %s has no meshes\n", filename);
        aiReleaseImport(scene);
        return NULL;
    }

    Material material = { 0 };
	Model* model = (Model*)malloc(sizeof(Model));

	model->meshIndices = (u32*)malloc(sizeof(u32) * scene->mNumMeshes);
	model->materialIndices = (u32*)malloc(sizeof(u32) * scene->mNumMeshes);
    model->meshCount = scene->mNumMeshes;
	model->name = (char*)malloc(sizeof(char) * MAX_NAME_SIZE);
	strncpy(model->name, filename, MAX_NAME_SIZE - 1);
	model->name[MAX_NAME_SIZE - 1] = '\0'; // Ensure null termination
	model->shaderCount = 0;
	model->shaders = (u32*)malloc(sizeof(u32) * scene->mNumMeshes);

    for (u32 m = 0; m < scene->mNumMeshes; m++)
    {
        //struct aiMesh* aimesh = scene->mMeshes[m];
        //u32 materialIndex = aimesh->mMaterialIndex;
        //struct aiMaterial* aiMat = scene->mMaterials[materialIndex];

         
         
    }

  
    aiReleaseImport(scene);
    return model;


}