#include "../../include/druid.h"
#include <string.h>

#include <stdio.h>
#define MAX_NAME_SIZE 256
#define MAX_FILE_SIZE 1024

ResourceManager *resources = NULL;

u32 djb2Hash(const void *inStr, u32 capacity)
{
    const c8 *str = (const c8 *)inStr;
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
    return strcmp((const c8 *)a, (const c8 *)b) == 0;
}

i32 compare_files_for_loading_priority(const void* a, const void* b) {
    const c8* fileA = *(const c8**)a;
    const c8* fileB = *(const c8**)b;

    const c8* extA = strrchr(fileA, '.');
    const c8* extB = strrchr(fileB, '.');

    if (!extA || extA == fileA) return 1;      // No extension on A, B goes first
    if (!extB || extB == fileB) return -1;     // No extension on B, A goes first

    extA++;
    extB++;

    b8 isTexA = (strcmp(extA, "png") == 0 || strcmp(extA, "jpg") == 0 || strcmp(extA, "bmp") == 0);
    b8 isTexB = (strcmp(extB, "png") == 0 || strcmp(extB, "jpg") == 0 || strcmp(extB, "bmp") == 0);

    if (isTexA && !isTexB) {
        return -1; // A (texture) comes before B
    }
    if (!isTexA && isTexB) {
        return 1; // B (texture) comes before A
    }

    // Use lexical order as a deterministic tie-breaker so model/shader indices
    // stay stable across runs and scene-stored IDs remain valid.
    return strcmp(fileA, fileB);
}

ResourceManager *createResourceManager(u32 materialCount, u32 textureCount,
                                       u32 meshCount, u32 modelCount,
                                       u32 shaderCount)
{
    ResourceManager *manager =
        (ResourceManager *)dalloc(sizeof(ResourceManager), MEM_TAG_RENDERER);
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
    manager->geoBuffer = NULL;  // created later in initSystems after GL context

    // allocate arrays
    manager->materialBuffer =
        (Material *)dalloc(sizeof(Material) * materialCount, MEM_TAG_MATERIAL);
    manager->meshBuffer = (Mesh *)dalloc(sizeof(Mesh) * meshCount, MEM_TAG_MESH);
    manager->modelBuffer = (Model *)dalloc(sizeof(Model) * modelCount, MEM_TAG_MODEL);
    manager->textureHandles = (u32 *)dalloc(sizeof(u32) * textureCount, MEM_TAG_TEXTURE);
    manager->shaderHandles = (u32 *)dalloc(sizeof(u32) * shaderCount, MEM_TAG_SHADER);

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
                   sizeof(c8) * MAX_NAME_SIZE, sizeof(u32), djb2Hash, equals))
    {
            FATAL("Texture Hash map failed to create");
    }
    if (!createMap(&manager->shaderIDs, shaderCount,
                   sizeof(c8) * MAX_NAME_SIZE, sizeof(u32), djb2Hash, equals))
    {
            FATAL("Shader Hash map failed to create");
    }
    // create hashmaps
    if (!createMap(&manager->materialIDs, materialCount,
                   sizeof(c8) * MAX_NAME_SIZE, sizeof(u32), djb2Hash, equals))
    {
            FATAL("Material Hash map failed to create");
    }
    if (!createMap(&manager->mesheIDs, meshCount, sizeof(c8) * MAX_NAME_SIZE,
                   sizeof(u32), djb2Hash, equals))
    {
        FATAL("Mesh Hash map failed to create");
    }
    if (!createMap(&manager->modelIDs, modelCount, sizeof(c8) * MAX_NAME_SIZE,
                   sizeof(u32), djb2Hash, equals))
    {
        FATAL("Model Hash map failed to create");
    }

    if(DEBUG_RESOURCES)
    DEBUG("Resource Manager created successfully\n");

    return manager;
}

void cleanUpResourceManager(ResourceManager *manager)
{
    if (!manager) return;
    if (manager->geoBuffer) geometryBufferDestroy(manager->geoBuffer);
    dfree(manager->materialBuffer, sizeof(Material) * manager->materialCount, MEM_TAG_MATERIAL);
    dfree(manager->meshBuffer, sizeof(Mesh) * manager->meshCount, MEM_TAG_MESH);
    dfree(manager->modelBuffer, sizeof(Model) * manager->modelCount, MEM_TAG_MODEL);
    dfree(manager->textureHandles, sizeof(u32) * manager->textureCount, MEM_TAG_TEXTURE);
    dfree(manager->shaderHandles, sizeof(u32) * manager->shaderCount, MEM_TAG_SHADER);
    dfree(manager, sizeof(ResourceManager), MEM_TAG_RENDERER);
}

DAPI void readResources(ResourceManager *manager, const c8 *filename)
{
    u32 modelExtCount = 3;
    // vert, frag, geom, glsl, comp
    u32 shaderExtCount = 5;
    u32 textureExtCount = 3;

    const c8 *fileExtentions[] = {"fbx",  "obj",  "blend", "vert",
                                    "frag", "geom", "glsl", "comp",
                                    "png",  "jpg",  "bmp"}; // textures

    if (!filename || filename[0] == '\0') filename = "../" RES_FOLDER;

    // Normalise: ensure trailing slash so path joins work correctly
    c8 resDir[512];
    strncpy(resDir, filename, sizeof(resDir) - 2);
    resDir[sizeof(resDir) - 2] = '\0';
    u32 rdLen = (u32)strlen(resDir);
    if (rdLen > 0 && resDir[rdLen - 1] != '/' && resDir[rdLen - 1] != '\\')
    {
        resDir[rdLen]     = '/';
        resDir[rdLen + 1] = '\0';
    }

    if(DEBUG_RESOURCES)
        TRACE("Reading resources from %s\n", resDir);
    // do a recursive search for all files in the given resource directory
    u32 outCount = 0;
    c8 **output = listFilesInDirectory(resDir, &outCount);
    if (output)
        qsort(output, outCount, sizeof(c8 *), compare_files_for_loading_priority);
    if(DEBUG_RESOURCES)
        INFO("Found %d files in directory %s\n", outCount, resDir);

    HashMap shaderNameMap;
    if (!createMap(&shaderNameMap, outCount > 0 ? outCount : 8, sizeof(c8) * MAX_NAME_SIZE,
                   sizeof(u32), djb2Hash, equals))
    {
            FATAL("Model Hash map failed to create");
        return;
    }
    //load default shader which is called "shader.*"
    const c8 *defaultShaderName = "default";

    // Build paths from the provided resource directory
    c8 defVertPath[512], defFragPath[512];
    snprintf(defVertPath, sizeof(defVertPath), "%sshader.vert", resDir);
    snprintf(defFragPath, sizeof(defFragPath), "%sshader.frag", resDir);

    //add default shader to resource manager (overwrite if already present from a prior call)
    u32 defaultShaderHandle = createGraphicsProgram(defVertPath, defFragPath);
    if (defaultShaderHandle != 0)
    {
        insertMap(&manager->shaderIDs, defaultShaderName, &manager->shaderUsed);
        manager->shaderHandles[manager->shaderUsed] = defaultShaderHandle;
        manager->shaderUsed++;
    } else
     {
        WARN("Failed to load default shader from %s", resDir);
    }
    
    if (outCount > 0)
    {
        // first pass populate shaderNameMap for vert/frag pairs
        for (u32 i = 0; i < outCount; i++)
        {
            const c8 *filePath = (const c8 *)output[i];
            c8 copyBuffer[512];
            strcpy(copyBuffer, filePath);

            c8 *fileName = strrchr(copyBuffer, '/');
            if (fileName)
                fileName++;
            else
                fileName = copyBuffer;

            c8 *ext = strrchr(fileName, '.');
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

        // second pass process all resources
        for (u32 i = 0; i < outCount; i++)
        {
            const c8 *filePath = (const c8 *)output[i];
            c8 copyBuffer[512];
            strcpy(copyBuffer, filePath);

            c8 *fileName = strrchr(copyBuffer, '/');
            if (fileName)
                fileName++;
            else
                fileName = copyBuffer;

            c8 *ext = strrchr(fileName, '.');
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
                        c8 pathNoExt[MAX_NAME_SIZE];
                        strncpy(pathNoExt, filePath, sizeof(pathNoExt) - 1);
                        pathNoExt[sizeof(pathNoExt) - 1] = '\0';
                        c8 *dot = strrchr(pathNoExt, '.');
                        if(dot) *dot = '\0';

                        c8 *shaderNameForUI = strrchr(pathNoExt, '/');
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
                        c8 shaderName[MAX_NAME_SIZE];
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

                                    c8 vertPath[MAX_NAME_SIZE];
                                    c8 fragPath[MAX_NAME_SIZE];
                                    c8 geomPath[MAX_NAME_SIZE];

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
                            if(DEBUG_RESOURCES)
                                TRACE("Loaded shader: %s", shaderName);
                            if(hasGeom)
                            {
                                if(DEBUG_RESOURCES)
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

                        c8 texName[MAX_NAME_SIZE];
                        snprintf(texName, MAX_NAME_SIZE, "%s", fileName);

                        insertMap(&manager->textureIDs, texName, &manager->textureUsed);
                        if(DEBUG_RESOURCES)
                            INFO("Loaded texture: %s", texName);
                        manager->textureUsed++;
                        break; 
                    }
                }
            }
        }
    }
    else
    {
        WARN("Failed to list files in directory %s\n", resDir);
    }

    for (u32 i = 0; i < outCount; i++)
    {
        free((c8 *)output[i]);
    }
    free(output);
    destroyMap(&shaderNameMap);
}

//=====================================================================================================================
// Typed resource getters  index-based
//=====================================================================================================================
Mesh *resGetMesh(u32 index)
{
    if (!resources || index >= resources->meshUsed) return NULL;
    return &resources->meshBuffer[index];
}

Model *resGetModel(u32 index)
{
    if (!resources || index >= resources->modelUsed) return NULL;
    return &resources->modelBuffer[index];
}

Material *resGetMaterial(u32 index)
{
    if (!resources || index >= resources->materialUsed) return NULL;
    return &resources->materialBuffer[index];
}

u32 resGetTexture(u32 index)
{
    if (!resources || index >= resources->textureUsed) return 0;
    return resources->textureHandles[index];
}

u32 resGetShader(u32 index)
{
    if (!resources || index >= resources->shaderUsed) return 0;
    return resources->shaderHandles[index];
}

//=====================================================================================================================
// Typed resource getters  name-based
//=====================================================================================================================
Mesh *resGetMeshByName(const c8 *name)
{
    if (!resources || !name) return NULL;
    u32 idx = 0;
    if (!findInMap(&resources->mesheIDs, name, &idx)) return NULL;
    return resGetMesh(idx);
}

Model *resGetModelByName(const c8 *name)
{
    if (!resources || !name) return NULL;
    u32 idx = 0;
    if (!findInMap(&resources->modelIDs, name, &idx)) return NULL;
    return resGetModel(idx);
}

Material *resGetMaterialByName(const c8 *name)
{
    if (!resources || !name) return NULL;
    u32 idx = 0;
    if (!findInMap(&resources->materialIDs, name, &idx)) return NULL;
    return resGetMaterial(idx);
}

u32 resGetTextureByName(const c8 *name)
{
    if (!resources || !name) return 0;
    u32 idx = 0;
    if (!findInMap(&resources->textureIDs, name, &idx)) return 0;
    return resGetTexture(idx);
}

u32 resGetShaderByName(const c8 *name)
{
    if (!resources || !name) return 0;
    u32 idx = 0;
    if (!findInMap(&resources->shaderIDs, name, &idx)) return 0;
    return resGetShader(idx);
}