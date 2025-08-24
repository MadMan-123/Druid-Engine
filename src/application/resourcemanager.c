
#include "../../include/druid.h"
#if PLATFORM_WINDOWS
#define NOGDI
#define NOERROR
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <stdio.h>
#define MAX_NAME_SIZE 256
#define MAX_FILE_SIZE 1024


u32 djb2Hash(const void* inStr, u32 capacity)
{
    const char* str = (const char*)inStr;
    u32 hash = 5381;
    i32 c;

    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % capacity;
}

b8 equals(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}

ResourceManager* createResourceManager(u32 materialCount,u32 textureCount ,u32 meshCount, u32 modelCount, u32 shaderCount)
{
    ResourceManager* manager = (ResourceManager*)malloc(sizeof(ResourceManager));
    if (manager == NULL)
    {
        ERROR("Resource Manager not allocated correctly");
        return NULL;
    } 

	//allocate members
	manager->materialCount = materialCount;
	manager->meshCount = meshCount;
	manager->modelCount = modelCount;
	manager->shaderCount = shaderCount;
	manager->textureCount = textureCount;
	//allocate arrays
    manager->materialBuffer = (Material*)malloc(sizeof(Material) * materialCount);
	manager->meshBuffer = (Mesh*)malloc(sizeof(Mesh) * meshCount);
	manager->modelBuffer = (Model*)malloc(sizeof(Model) * modelCount);
	manager->textureHandles = (u32*)malloc(sizeof(u32) * textureCount); 
	manager->shaderHandles = (u32*)malloc(sizeof(u32) * shaderCount);

    //null check
	assert(manager->materialBuffer && "material buffers not created");
	assert(manager->meshBuffer && "mesh buffers not created");
	assert(manager->modelBuffer && "model buffers not created");
    assert(manager->textureHandles && "texture handles not created");
	assert(manager->shaderHandles && "shader handles not created");

    //create hashmaps
    if (!createMap(&manager->textureIDs,
        textureCount,
        sizeof(char) * MAX_NAME_SIZE ,
        sizeof(u32),
        djb2Hash,
        equals)
        )
    {
        FATAL("Texture Hash map failed to create");
    }
    if (!createMap(&manager->shaderIDs,
        shaderCount,
        sizeof(char) * MAX_NAME_SIZE,
        sizeof(u32),
        djb2Hash,
        equals)
        )
    {
        FATAL("Shader Hash map failed to create");
	}
	//create hashmaps
    if (!createMap(&manager->materialIDs,
        materialCount,
        sizeof(char) * MAX_NAME_SIZE,
        sizeof(u32),
        djb2Hash,
        equals)
        )
    {
		FATAL("Material Hash map failed to create");
    }
    if (!createMap(&manager->mesheIDs,
        meshCount,
        sizeof(char) * MAX_NAME_SIZE,
        sizeof(u32),
        djb2Hash,
        equals)
        )
    {
        FATAL("Mesh Hash map failed to create");
    }
    if( !createMap(&manager->modelIDs,
        modelCount,
        sizeof(char) * MAX_NAME_SIZE,
        sizeof(u32),
        djb2Hash,
        equals)
        )
    {
        FATAL("Model Hash map failed to create");
	
     }
      
	DEBUG("Resource Manager created successfully\n");


    return manager;
}

void cleanUpResourceManager(ResourceManager* manager)
{
	//free all resources in the manager
	free(manager->materialBuffer);
	free(manager->meshBuffer);
	free(manager->modelBuffer);
	free(manager->textureHandles);
	free(manager->shaderHandles);
	free(manager);

}

void addModel(ResourceManager* manager, const char* filename)
{
	//load the model from file
	//check if we have space
    if (manager->modelUsed >= manager->modelCount)
    {
        ERROR("Model buffer full, cannot add more models");
        return;
	}

	Model* model = loadModelFromAssimp(manager,filename);
    if (model == NULL)
    {
        ERROR("Failed to load model %s", filename);
        return;
	}
	//add to manager
	manager->modelBuffer[manager->modelUsed] = *model;
	manager->modelUsed++;

	DEBUG("Model %s loaded successfully with %d meshes\n", model->meshCount);

	//add to hashmap using filename as key
	insertMap(&manager->modelIDs, filename, &manager->modelUsed);
}

void readResources(ResourceManager* manager, const char* filename)
{
    const u32 modelExtentionCount = 3;
    const u32 shaderExtentionCount = 4;
    const u32 textureExtentionCount = 3;

    const char* modelExtentions[] = {
    ".fbx",".obj",".blend"
    }; //models
    const char* shaderExtentions[] = {
    ".vert",".frag",".glsl",".comp" 
    }; 
    const char* textureExtentions[] = {
    ".png",".jpg",".bmp" 
    };//textures
    
    // This function should read resources from a file and populate the ResourceManager
	TRACE("Reading resources from %s\n", filename);
	//do a recursive search for all files in the RES_FOLDER directory
	const u32 bufferSize = 1024;
    u32 outCount = 0;
    char** output = listFilesInDirectory("..\\"RES_FOLDER, &outCount);

    //as the resources will not be in the thousounds for a while this approach works well,
    // note that as we progress in size we may need to create some O(1) solution
	//list all files in the directory and take it to output
    if (outCount > 0)
    {
        for (u32 i = 0; i < outCount; i++)
        {
            //read the file extension
            const char* filePath = output[i];
            const char* ext = strrchr(filePath, '.');

            b8 isModel = false, isShader = false, isTexture = false;
            for (u32 m = 0; m < modelExtentionCount; m++)
            {
                //compare model extention
                isModel = strcmp(ext, modelExtentions[m]) == 0;
                if (isModel)
                {
                    INFO("Found Model %s", filePath);
					addModel(manager, filePath);
                    goto SKIP;
                }
            }
            for (u32 s = 0; s < shaderExtentionCount; s++)
            {
                isShader = strcmp(ext, shaderExtentions[s]) == 0;
                if (isShader) goto SKIP;
            }

            for (u32 t = 0; t < textureExtentionCount; t++)
            {
                isTexture = strcmp(ext, textureExtentions[t]) == 0;
                if (isTexture) goto SKIP;
            }

        }
    }
    else
    {
        WARN("Failed to list files in directory %s\n", "..\\"RES_FOLDER);
	}

SKIP:
    //free the output list
    for (u32 i = 0; i < outCount; i++)
    {
        free(output[i]);
    }
	free(output);

}


#if PLATFORM_WINDOWS
void listFilesRecursive(const char* directory, char*** fileList, u32* count, u32* capacity)
{
    WIN32_FIND_DATA findFileData;
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\*", directory);

    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        const char* name = findFileData.cFileName;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;


        char fullPath[MAX_PATH];
        snprintf(fullPath, MAX_PATH, "%s\\%s", directory, name);
		normalizePath(fullPath);

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) 
        {
            // recurse into subdir
            listFilesRecursive(fullPath, fileList, count, capacity);
        }
        else 
        {
            // store file
            if (*count >= *capacity) 
            {
                WARN("File list capacity exceeded");
                break;
            }
            (*fileList)[*count] = _strdup(fullPath); 
			TRACE("Found file: %s", fullPath);
            (*count)++;
        }
    } while (FindNextFile(hFind, &findFileData));

    FindClose(hFind);
}
#elif PLATFORM_LINUX || PLATFORM_MAC
    
#endif

char** listFilesInDirectory(const char* directory,u32* outCount)
{
    u32 capacity = 256;
    char** fileList = (char**)malloc(sizeof(char*) * capacity);
	*outCount = 0;
	
	listFilesRecursive(directory, &fileList, outCount, &capacity);

	return fileList;
}
void normalizePath(char* path)
{
    char* src = path;
    char* dst = path;

    while (*src) {
        //convert backslash to slash
        char c = (*src == '\\') ? '/' : *src;

        //collapse multiple '/'
        if (c == '/' && dst > path && dst[-1] == '/') {
            src++;
            continue;
        }

        *dst++ = c;
        src++;
    }

    *dst = '\0';
}

