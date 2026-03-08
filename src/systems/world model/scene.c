#include "../../../include/druid.h"
#include <stdio.h>
#include <stdlib.h>

SceneData bakeScene(Scene *scene)
{
    SceneData md = {0};

    // iterate through the scene archetypes and copy the transform data into the
    // saved archetype

    return md;
}

SceneManager *createSceneManager(u32 sceneCapacity)
{

    SceneManager *manager = (SceneManager *)malloc(sizeof(SceneManager));
    if (!manager)
    {
        ERROR("Failed to allocate memory for SceneManager");
        return NULL;
    }

    // create an arena for scene data
    manager->data = (Arena *)malloc(sizeof(Arena));
    if (!manager->data)
    {
        ERROR("Failed to allocate memory for SceneManager arena");
        free(manager);
        return NULL;
    }

    // 1024 bytes extra
    const u32 EXTRA_DATA = 1024;

    if (!arenaCreate(manager->data,
                     sizeof(SceneManager) +
                         (sizeof(SceneData) + (EXTRA_DATA)*sceneCapacity +
                          sizeof(Scene))))
    {
        ERROR("Failed to create arena for SceneManager");
        free(manager->data);
        free(manager);
        return NULL;
    }
    manager->scenes =
        (SceneData *)aalloc(manager->data, sizeof(SceneData) * sceneCapacity);
    if (!manager->scenes)
    {
        ERROR("Failed to allocate memory for SceneData array");
        free(manager);
        return NULL;
    }

    manager->sceneCapacity = sceneCapacity;
    manager->sceneCount = 0;

    // allocate the current scene as an empty scene
    manager->currentScene = (Scene *)aalloc(manager->data, sizeof(Scene));
    if (!manager->currentScene)
    {
        ERROR("Failed to allocate memory for current Scene");
        arenaDestroy(manager->data);
        free(manager);
        return NULL;
    }

    return manager;
}

void destroySceneManager(SceneManager *manager)
{
    // free all data associated with the scene manager
    if (!manager)
        return;

    arenaDestroy(manager->data);

    DEBUG("Destroyed scene manager");
}

u32 addScene(SceneManager *manager, SceneData *sceneData)
{
    // TODO: add scene (grow array if necessary)
    if (manager->sceneCount >= manager->sceneCapacity)
    {
        WARN("SceneManager capacity reached, cannot add more scenes");
        return (u32)-1;
    }

    // copy the scene data into the manager
    manager->scenes[manager->sceneCount] = *sceneData;
    manager->sceneCount++;
    return manager->sceneCount - 1;
}

void removeScene(SceneManager *manager, u32 sceneIndex)
{
    // goto the index and remove it, shift all scenes down
    if (sceneIndex >= manager->sceneCount)
    {
        WARN("Invalid scene index %d for removal", sceneIndex);
        return;
    }

    // remove the scene
    manager->scenes[sceneIndex] = (SceneData){0};

    // shift all scenes down from the removed index up to sceneCount - 1
    for (u32 i = sceneIndex; i < manager->sceneCount - 1; i++)
    {
        manager->scenes[i] = manager->scenes[i + 1];
    }
    manager->sceneCount--;
}

void switchScene(SceneManager *manager, u32 sceneIndex)
{
    // TODO: set current scene and (optionally) apply to runtime
}

void saveScene(const c8 *filePath, SceneData *data)
{
    // TODO: persist scene metadata to disk (JSON or binary)
}

SceneData loadScene(const c8 *filePath)
{
    SceneData out = {0};
    // TODO: load scene metadata from disk and populate out
    return out;
}
