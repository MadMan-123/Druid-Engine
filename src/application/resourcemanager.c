
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
    u32 shaderExtCount = 4;
    u32 textureExtCount = 3;

    const char *fileExtentions[] = {"fbx",  "obj",  "blend", "vert",
                                    "frag", "glsl", "comp",  "png",
                                    "jpg",  "bmp"}; // textures

    TRACE("Reading resources from %s\n", filename);
    // do a recursive search for all files in the RES_FOLDER directory
    const u32 bufferSize = 1024;
    u32 outCount = 0;
    char **output = listFilesInDirectory("..\\" RES_FOLDER, &outCount);
    INFO("Found %d files in directory %s\n", outCount, "..\\" RES_FOLDER);
    // as the resources will not be in the thousounds for a while this approach
    // works well,
    // note that as we progress in size we may need to create some O(1)
    // solution
    // list all files in the directory and take it to output

    HashMap shaderNameMap;
    if (!createMap(&shaderNameMap, outCount, sizeof(char) * MAX_NAME_SIZE,
                   sizeof(u32), djb2Hash, equals))
    {
        FATAL("Model Hash map failed to create");
    }

    if (outCount > 0)
    {
        for (u32 i = 0; i < outCount; i++)
        {
            //  read the file extension
            const char *filePath = output[i];
            // create a copy buffer to get the rest of the data
            char copyBuffer[512];
            strcpy(copyBuffer, output[i]);
            const char *fileName = strrchr(copyBuffer, '/');
            // remove the first slash
            if (fileName)
                fileName++;
            else
                fileName = filePath;

            char delimiter[] = ".";
            const char *ext = strtok(fileName, delimiter);
            ext = strtok(NULL, delimiter);

            char *nameNoExt = strtok(fileName, delimiter);

            // check if its a shader

            if ((strcmp(ext, "frag") == 0) || (strcmp(ext, "vert") == 0))
            {
                u32 value = 0;
                // incrament the map value
                if (findInMap(&shaderNameMap, copyBuffer, &value))
                {
                    value++;
                }
                else
                {
                    value = 1;
                }

                insertMap(&shaderNameMap, copyBuffer, &value);
            }
        }

        for (u32 i = 0; i < outCount; i++)
        {
            DEBUG("File %d: %s\n", i, output[i]);
            //  read the file extension
            const char *filePath = output[i];
            // create a copy buffer to get the rest of the data
            char copyBuffer[512];
            strcpy(copyBuffer, output[i]);
            const char *fileName = strrchr(copyBuffer, '/');
            // remove the first slash
            if (fileName)
                fileName++;
            else
                fileName = filePath;

            char delimiter[] = ".";
            const char *ext = strtok(fileName, delimiter);
            ext = strtok(NULL, delimiter);

            char *nameNoExt = strtok(fileName, delimiter);

            for (u32 m = 0;
                 m < modelExtCount + shaderExtCount + textureExtCount; m++)
            {

                // check if the extension matches any known types
                // if it does save the index of what type it is
                // work out what type of resource it is depending on the index
                u32 result = strcmp(ext, fileExtentions[m]);
                // print the result and extension
                /*
                INFO("Checking file %s with extension %s against %s, result:% "
                     "d\n",
                     filePath, ext ? ext : " No Extension ", fileExtentions[m],
                     result);
                */
                if (ext && result == 0)
                {
                    if (m < modelExtCount)
                    {
                        // TODO: add a check if we can actually add to the
                        // resourece manager
                        //  load the model to the resource manager
                        Model *loadedModel =
                            loadModelFromAssimp(manager, filePath);
                        // add model to the map

                        char modelName[MAX_NAME_SIZE];
                        snprintf(modelName, MAX_NAME_SIZE, "model-%d",
                                 manager->modelUsed);
                        insertMap(&manager->modelIDs, modelName,
                                  &manager->modelUsed);
                        // set the model to the model buffer
                        memcpy(&manager->modelBuffer[manager->modelUsed],
                               loadedModel, sizeof(Model));
                        manager->modelUsed++;

                        // free the model
                        free(loadedModel);
                    }
                    else if (m < modelExtCount + shaderExtCount)
                    {
                        u32 temp = 0;
                        if (findInMap(&manager->shaderIDs, nameNoExt, &temp))
                        {
                            // if the value is at 1 or 2 then skip
                            if (temp >= 1)
                                continue;
                        }
                        // load shader program depending on if its a compute
                        // shader or graphics shader
                        u32 shaderHandle = 0;

                        char shaderName[MAX_NAME_SIZE];
                        if (strcmp(ext, "comp") == 0)
                        {
                            shaderHandle = createComputeProgram(filePath);
                            //  add to the hashmap
                            //  TODO: also validate can add
                            snprintf(shaderName, MAX_NAME_SIZE,
                                     "compute-shader-%d", manager->shaderUsed);
                        }
                        // frag will always be first
                        else if ((strcmp(ext, "frag") == 0) ||
                                 (strcmp(ext, "vert") == 0))
                        {
                            // check if they should be created seperately or
                            // individually
                            u32 out = 0;
                            if (findInMap(&shaderNameMap, copyBuffer, &out))
                            {
                                snprintf(shaderName, MAX_NAME_SIZE, "%s",
                                         nameNoExt);

                                char vertPath[MAX_NAME_SIZE];
                                char fragPath[MAX_NAME_SIZE];

                                // setup the paths
                                snprintf(vertPath, MAX_NAME_SIZE, "%s.%s",
                                         copyBuffer, "vert");

                                snprintf(fragPath, MAX_NAME_SIZE, "%s.%s",
                                         copyBuffer, "frag");
                                shaderHandle =
                                    createGraphicsProgram(vertPath, fragPath);
                            }
                        }

                        insertMap(&manager->shaderIDs, shaderName,
                                  &manager->shaderUsed);

                        // set the shader to the shader buffers
                        manager->shaderHandles[manager->shaderUsed] =
                            shaderHandle;

                        manager->shaderUsed++;
                    }
                    else
                    {
                        // load texture
                    }
                }
            }
        }
    }
    else
    {
        WARN("Failed to list files in directory %s\n", "..\\" RES_FOLDER);
    }

    // free the output list
    // for (u32 i = 0; i < outCount; i++)
    //{
    //    free(output[i]);
    //}
    free(output);
    destroyMap(&shaderNameMap);
}

#if PLATFORM_WINDOWS
void listFilesRecursive(const char *directory, char ***fileList, u32 *count,
                        u32 *capacity)
{
    WIN32_FIND_DATA findFileData;
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\*", directory);

    HANDLE hFind = FindFirstFile(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        const char *name = findFileData.cFileName;

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
