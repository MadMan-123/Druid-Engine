#include "../../include/druid.h"
#include <string.h>
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

ResourceManager *resources = NULL;

u32 djb2Hash(const void *inStr, u32 capacity)
{
    const char *str = (const char *)inStr;
    u32 hash = 5381;
    i32 c;

    while ((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % capacity;
}

b8 equals(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b) == 0;
}

int compare_files_for_loading_priority(const void* a, const void* b) {
    const char* fileA = *(const char**)a;
    const char* fileB = *(const char**)b;

    const char* extA = strrchr(fileA, '.');
    const char* extB = strrchr(fileB, '.');

    if (!extA || extA == fileA) return 1;      // No extension on A, B goes first
    if (!extB || extB == fileB) return -1;     // No extension on B, A goes first

    extA++;
    extB++;

    bool isTexA = (strcmp(extA, "png") == 0 || strcmp(extA, "jpg") == 0 || strcmp(extA, "bmp") == 0);
    bool isTexB = (strcmp(extB, "png") == 0 || strcmp(extB, "jpg") == 0 || strcmp(extB, "bmp") == 0);

    if (isTexA && !isTexB) {
        return -1; // A (texture) comes before B
    }
    if (!isTexA && isTexB) {
        return 1; // B (texture) comes before A
    }
    return 0; // Keep original order for same types
}

ResourceManager *createResourceManager(u32 materialCount, u32 textureCount,
                                       u32 meshCount, u32 modelCount,
                                       u32 shaderCount)
{
    ResourceManager *manager =
        (ResourceManager *)malloc(sizeof(ResourceManager));
    if (manager == NULL)
    {
        ERROR("Resource Manager not allocated correctly");
        return NULL;
    }

    // TODO: create a resourcemanager metadata structure to contain this shit
    //  allocate members
    manager->materialCount = materialCount;
    manager->meshCount = meshCount;
    manager->modelCount = modelCount;
    manager->shaderCount = shaderCount;
    manager->textureCount = textureCount;

    manager->meshUsed = 0;
    manager->materialUsed = 0;
    manager->modelUsed = 0;
    manager->shaderUsed = 0;
    manager->textureUsed = 0;

    // allocate arrays
    manager->materialBuffer =
        (Material *)malloc(sizeof(Material) * materialCount);
    manager->meshBuffer = (Mesh *)malloc(sizeof(Mesh) * meshCount);
    manager->modelBuffer = (Model *)malloc(sizeof(Model) * modelCount);
    manager->textureHandles = (u32 *)malloc(sizeof(u32) * textureCount);
    manager->shaderHandles = (u32 *)malloc(sizeof(u32) * shaderCount);

    // null check
    assert(manager->materialBuffer != NULL && "material buffers not created");
    assert(manager->meshBuffer != NULL && "mesh buffers not created");
    assert(manager->modelBuffer != NULL && "model buffers not created");
    assert(manager->textureHandles != NULL && "texture handles not created");
    assert(manager->shaderHandles != NULL && "shader handles not created");

    //    DEBUG("mangeger pointers: %p, %p, %p, 
    //    %p",manager->materialBuffer,manager->meshBuffer);

    // create hashmaps
    if (!createMap(&manager->textureIDs, textureCount,
                   sizeof(char) * MAX_NAME_SIZE, sizeof(u32), djb2Hash, equals))
    {
        FATAL("Texture Hash map failed to create");
    }
    if (!createMap(&manager->shaderIDs, shaderCount,
                   sizeof(char) * MAX_NAME_SIZE, sizeof(u32), djb2Hash, equals))
    {
        FATAL("Shader Hash map failed to create");
    }
    // create hashmaps
    if (!createMap(&manager->materialIDs, materialCount,
                   sizeof(char) * MAX_NAME_SIZE, sizeof(u32), djb2Hash, equals))
    {
        FATAL("Material Hash map failed to create");
    }
    if (!createMap(&manager->mesheIDs, meshCount, sizeof(char) * MAX_NAME_SIZE,
                   sizeof(u32), djb2Hash, equals))
    {
        FATAL("Mesh Hash map failed to create");
    }
    if (!createMap(&manager->modelIDs, modelCount, sizeof(char) * MAX_NAME_SIZE,
                   sizeof(u32), djb2Hash, equals))
    {
        FATAL("Model Hash map failed to create");
    }

    DEBUG("Resource Manager created successfully\n");

    return manager;
}

void cleanUpResourceManager(ResourceManager *manager)
{
    // free all resources in the manager
    free(manager->materialBuffer);
    free(manager->meshBuffer);
    free(manager->modelBuffer);
    free(manager->textureHandles);
    free(manager->shaderHandles);
    free(manager);
}

void readResources(ResourceManager *manager, const char *filename)
{
    u32 modelExtCount = 3;
    // vert, frag, geom, glsl, comp
    u32 shaderExtCount = 5;
    u32 textureExtCount = 3;

    const char *fileExtentions[] = {"fbx",  "obj",  "blend", "vert",
                                    "frag", "geom", "glsl", "comp",
                                    "png",  "jpg",  "bmp"}; // textures

    TRACE("Reading resources from %s\n", filename);
    // do a recursive search for all files in the RES_FOLDER directory
    u32 outCount = 0;
    char **output = listFilesInDirectory("../" RES_FOLDER, &outCount);
    qsort(output, outCount, sizeof(char*), compare_files_for_loading_priority);
    INFO("Found %d files in directory %s\n", outCount, "../" RES_FOLDER);

    HashMap shaderNameMap;
    if (!createMap(&shaderNameMap, outCount, sizeof(char) * MAX_NAME_SIZE,
                   sizeof(u32), djb2Hash, equals))
    {
        FATAL("Model Hash map failed to create");
    }

    //load default shader which is called "shader.*"

    const char *defaultShaderName = "default";
    
    //add default shader to resource manager
    u32 defaultShaderHandle = createGraphicsProgram("../" RES_FOLDER "/shader.vert", "../" RES_FOLDER "/shader.frag");
    if (defaultShaderHandle != 0) 
    {
        insertMap(&manager->shaderIDs, defaultShaderName, &manager->shaderUsed);
        manager->shaderHandles[manager->shaderUsed] = defaultShaderHandle;
        manager->shaderUsed++;
    } else
     {
        WARN("Failed to load default shader");
    }
    
    if (outCount > 0)
    {
        // First pass: populate shaderNameMap for vert/frag pairs
        for (u32 i = 0; i < outCount; i++)
        {
            const char *filePath = output[i];
            char copyBuffer[512];
            strcpy(copyBuffer, filePath);

            char *fileName = strrchr(copyBuffer, '/');
            if (fileName)
                fileName++;
            else
                fileName = copyBuffer;

            char *ext = strrchr(fileName, '.');
            // include .vert, .frag, .geom and .glsl in the first-pass grouping
            if (ext && (strcmp(ext, ".vert") == 0 || strcmp(ext, ".frag") == 0 || strcmp(ext, ".geom") == 0 || strcmp(ext, ".glsl") == 0))
            {
                *ext = '\0'; // Strip extension for shader name grouping
                u32 value = 0;
                if (findInMap(&shaderNameMap, fileName, &value))
                {
                    value++;
                }
                else
                {
                    value = 1;
                }   
                insertMap(&shaderNameMap, fileName, &value);
            }
        }

        // Second pass: process all resources
        for (u32 i = 0; i < outCount; i++)
        {
            const char *filePath = output[i];
            char copyBuffer[512];
            strcpy(copyBuffer, filePath);

            char *fileName = strrchr(copyBuffer, '/');
            if (fileName)
                fileName++;
            else
                fileName = copyBuffer;

            char *ext = strrchr(fileName, '.');
            if (!ext || ext == fileName)
                continue;
            ext++;

            for (u32 m = 0; m < modelExtCount + shaderExtCount + textureExtCount; m++)
            {
                if (strcmp(ext, fileExtentions[m]) == 0)
                {
                    if (m < modelExtCount)
                    {
                        loadModelFromAssimp(manager, filePath);
                        break; 
                    }
                    else if (m < modelExtCount + shaderExtCount)
                    {
                        char pathNoExt[MAX_NAME_SIZE];
                        strncpy(pathNoExt, filePath, sizeof(pathNoExt) - 1);
                        pathNoExt[sizeof(pathNoExt) - 1] = '\0';
                        char *dot = strrchr(pathNoExt, '.');
                        if(dot) *dot = '\0';

                        char *shaderNameForUI = strrchr(pathNoExt, '/');
                        if (shaderNameForUI)
                            shaderNameForUI++;
                        else
                            shaderNameForUI = pathNoExt;

                        u32 temp = 0;
                        // if this shader name is already present in the manager, skip it
                        if (findInMap(&manager->shaderIDs, shaderNameForUI, &temp))
                        {
                            continue;
                        }
                        // initial extension flag (used only for early checks)
                        
                        b8 hasGeom = false;
                        u32 shaderHandle = 0;
                        char shaderName[MAX_NAME_SIZE];
                        if (strcmp(ext, "comp") == 0)
                        {
                            shaderHandle = createComputeProgram(filePath);
                            snprintf(shaderName, MAX_NAME_SIZE, "compute-shader-%d", manager->shaderUsed);
                        }
                        else if (strcmp(ext, "vert") == 0 || strcmp(ext, "frag") == 0 || strcmp(ext, "geom") == 0 || strcmp(ext, "glsl") == 0 || strcmp(ext, "geom") == 0  )
                        {
                            u32 out = 0;
                            // shaderNameMap keys were stored as filenames (no path), so use shaderNameForUI
                            if (findInMap(&shaderNameMap, shaderNameForUI, &out))
                            {
                                    snprintf(shaderName, MAX_NAME_SIZE, "%s", shaderNameForUI);

                                    char vertPath[MAX_NAME_SIZE];
                                    char fragPath[MAX_NAME_SIZE];
                                    char geomPath[MAX_NAME_SIZE];

                                    snprintf(vertPath, MAX_NAME_SIZE, "%s.vert", pathNoExt);
                                    snprintf(fragPath, MAX_NAME_SIZE, "%s.frag", pathNoExt);
                                    snprintf(geomPath, MAX_NAME_SIZE, "%s.geom", pathNoExt);

                                    // check if a geometry shader file exists for this base name
                                    FILE *f = fopen(geomPath, "r");
                                    if (f) {
                                        hasGeom = true;
                                        fclose(f);
                                    }

                                    if (hasGeom)
                                    {
                                        TRACE("loading geom shader");
                                        shaderHandle = createGraphicsProgramWithGeometry(vertPath, geomPath, fragPath);
                                    }
                                    else
                                    {
                                        shaderHandle = createGraphicsProgram(vertPath, fragPath);
                                    }

                            }
                        }

                        if(shaderHandle != 0) 
                        {
                            TRACE("Loaded shader: %s", shaderName);
                            if(hasGeom)
                            {
                                TRACE("Custom geometry shader added");
                            }
                            insertMap(&manager->shaderIDs, shaderName, &manager->shaderUsed);
                            manager->shaderHandles[manager->shaderUsed] = shaderHandle;
                            manager->shaderUsed++;
                        }
                        break; 
                    }
                    else
                    {
                        if (manager->textureUsed >= manager->textureCount)
                        {
                            FATAL("Resource manager is full and cant add texture");
                            continue;
                        }

                        u32 texture = initTexture(filePath);
                        if (texture == 0) {
                            WARN("Failed to load texture: %s", filePath);
                            continue;
                        }
                        manager->textureHandles[manager->textureUsed] = texture;

                        char texName[MAX_NAME_SIZE];
                        snprintf(texName, MAX_NAME_SIZE, "%s", fileName);

                        insertMap(&manager->textureIDs, texName, &manager->textureUsed);
                        manager->textureUsed++;
                        break; 
                    }
                }
            }
        }
    }
    else
    {
        WARN("Failed to list files in directory %s\n", "../" RES_FOLDER);
    }

    for (u32 i = 0; i < outCount; i++)
    {
        free(output[i]);
    }
    free(output);
    destroyMap(&shaderNameMap);
}

#if PLATFORM_WINDOWS
void listFilesRecursive(const char *directory, char ***fileList, u32 *count, 
                        u32 *capacity)
{
    WIN32_FIND_DATA findFileData;
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s/*", directory);

    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const char *name = findFileData.cFileName;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        char fullPath[MAX_PATH];
        snprintf(fullPath, MAX_PATH, "%s/%s", directory, name);
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

char **listFilesInDirectory(const char *directory, u32 *outCount)
{
    u32 capacity = 256;
    char **fileList = (char **)malloc(sizeof(char *) * capacity);
    *outCount = 0;

    listFilesRecursive(directory, &fileList, outCount, &capacity);

    return fileList;
}
void normalizePath(char *path)
{
    char *src = path;
    char *dst = path;

    while (*src)
    {
        // convert backslash to slash
        char c = (*src == '\\') ? '/' : *src;

        // collapse multiple '/'
        if (c == '/' && dst > path && dst[-1] == '/')
        {
            src++;
            continue;
        }

        *dst++ = c;
        src++;
    }

    *dst = '\0';
}
