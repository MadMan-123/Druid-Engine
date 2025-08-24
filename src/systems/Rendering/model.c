#include "druid.h"
#include <math.h>
#include <time.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#define MAX_NAME_SIZE 64
Model* loadModelFromAssimp(ResourceManager* manager,const char* filename)
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
        ERROR("Failed to load model %s: %s\n", filename, aiGetErrorString());
        aiReleaseImport(scene);
        return NULL;
    }

    //check if there are any meshes
    if (scene->mNumMeshes == 0)
    {
        ERROR("Model %s has no meshes\n", filename);
        aiReleaseImport(scene);
        return NULL;
    }

    Material* materials = (Material*)malloc(sizeof(Material) * scene->mNumMaterials);
    if (materials == NULL)
    {
        ERROR("Material buffer failed to create");
        return NULL;
    }

	//create materials
    for (u32 i = 0; i < scene->mNumMaterials; i++)
    {
		//allocate material
        materials[i] = (Material){ 0 };
        struct aiMaterial* aimat = scene->mMaterials[i];
		//get the file path
        
        readMaterial(&materials[i], aimat, "../"TEXTURE_FOLDER);
        
		//add material to resource manager
		manager->materialBuffer[i] = materials[i];
        manager->materialUsed++;

		//add material to hash map
		char matName[MAX_NAME_SIZE];
		snprintf(matName, MAX_NAME_SIZE, "%d-material",i);
		insertMap(&manager->materialIDs, matName, &i);

		
	}

	Model* model = (Model*)malloc(sizeof(Model));

	model->meshIndices = (u32*)malloc(sizeof(u32) * scene->mNumMeshes);
	model->materialIndices = (u32*)malloc(sizeof(u32) * scene->mNumMeshes);
    model->meshCount = scene->mNumMeshes;
	model->name = (char*)malloc(sizeof(char) * MAX_NAME_SIZE);
	strncpy(model->name, filename, MAX_NAME_SIZE - 1);
	model->name[MAX_NAME_SIZE - 1] = '\0'; //ensure null termination
	model->shaderCount = 0;
	model->shaders = (u32*)malloc(sizeof(u32) * scene->mNumMeshes);

    u32 count = 0;
	//load meshes to the resource manager
    Mesh* meshes = loadMeshFromAssimp(filename,&count);
    //check if the path is valid

    if (meshes == NULL)
    {
        ERROR("Mesh buffer failed to create");
        return NULL;
    }
    
    const u32 meshLength = scene->mNumMeshes;
    const u32 lineLength = 1024;
    char names[meshLength][lineLength];
    //get a list of mesh names
    for (u32 i = 0; i < meshLength; i++)
    {
		struct aiMesh* aimesh = scene->mMeshes[i];
        //string duplication
        *names[i] = *strdup(aimesh->mName.data);
        //add the model name to the names[i]
		*names[i] = *strcat(names[i], "-mesh");
         
		DEBUG("Mesh %d name: %s\n", i, names[i]);
    }


	//duplicate meshes to resource manager and model
    for (u32 i = 0; i < scene->mNumMeshes; i++)
    {
        //add mesh to resource manager
        manager->meshBuffer[manager->meshUsed] = meshes[i];
        //add to hash map
        insertMap(&manager->mesheIDs, names[i], &manager->meshUsed);
        model->meshIndices[i] = manager->meshUsed;
        model->materialIndices[i] = scene->mMeshes[i]->mMaterialIndex;
        model->shaders[i] = 0; //TODO: SETUP SHADERS FOR MATERIALS
        manager->meshUsed++;
	}
    

  
    aiReleaseImport(scene);
    return model;


}