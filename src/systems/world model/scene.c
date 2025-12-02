#include "../../../include/druid.h"
#include <stdio.h>
#include <stdlib.h>



SceneMetaData bakeScene(Scene* scene)
{
    SceneMetaData md = {0};
    
    //iterate through the scene archetypes and copy the transform data into the saved archetype
    for (u32 i = 0; i < scene->archetypeCount; i++)
    {
        Archetype* archetype = &scene->archetypes[i];
        if(!archetype)
            continue;

        //get the positions, rotations, scales fields and what archetype 
        


    }

    return md;
}

SceneManager* createSceneManager(u32 sceneCapacity)
{

    // TODO: allocate and initialize SceneManager
    SceneManager* manager = (SceneManager*)malloc(sizeof(SceneManager));
    if (!manager)
    {
        ERROR("Failed to allocate memory for SceneManager");
        return NULL;
    }


    //create an arena for scene data
    manager->data = (Arena*)malloc(sizeof(Arena));
    if (!manager->data)
    {
        ERROR("Failed to allocate memory for SceneManager arena");
        free(manager);
        return NULL;
    }

    if (!arenaCreate(manager->data, sizeof(SceneManager) + sizeof(SceneMetaData) * sceneCapacity + sizeof(Scene) ))
    {
        ERROR("Failed to create arena for SceneManager");
        free(manager->data);
        free(manager);
        return NULL;
    }
    manager->scenes = (SceneMetaData*)aalloc(manager->data, sizeof(SceneMetaData) * sceneCapacity);
    if (!manager->scenes)
    {
        ERROR("Failed to allocate memory for SceneMetaData array");
        free(manager);
        return NULL;
    }

    manager->sceneCapacity = sceneCapacity;
    manager->sceneCount = 0;

    //allocate the current scene as an empty scene
    manager->currentScene = (Scene*)aalloc(manager->data, sizeof(Scene));
    if (!manager->currentScene)
    {
        ERROR("Failed to allocate memory for current Scene");
        arenaDestroy(manager->data);
        free(manager);
        return NULL;
    }

    


    return manager;
}

void destroySceneManager(SceneManager* manager)
{
    // free all data associated with the scene manager
    if (!manager)
        return;

    arenaDestroy(manager->data);

    DEBUG("Destroyed scene manager");    
}

u32 addScene(SceneManager* manager, SceneMetaData* sceneData)
{
    // TODO: add scene (grow array if necessary)
    if (manager->sceneCount >= manager->sceneCapacity)
    {
        WARN("SceneManager capacity reached, cannot add more scenes");
        return (u32)-1;
    }

    //copy the scene data into the manager
    manager->scenes[manager->sceneCount] = *sceneData;
    manager->sceneCount++;
    return manager->sceneCount - 1;
}

void removeScene(SceneManager* manager, u32 sceneIndex)
{
    //goto the index and remove it, shift all scenes down
    if (sceneIndex >= manager->sceneCount)
    {
        WARN("Invalid scene index %d for removal", sceneIndex);
        return;
    } 

    //remove the scene 
    manager->scenes[sceneIndex] = (SceneMetaData){0};

    //shift all scenes down from the removed index up to sceneCount - 1
    for (u32 i = sceneIndex; i < manager->sceneCount - 1; i++)
    {
        manager->scenes[i] = manager->scenes[i + 1];
    }
    manager->sceneCount--;

}

void switchScene(SceneManager* manager, u32 sceneIndex)
{
    // TODO: set current scene and (optionally) apply to runtime
}

void saveScene(const char *filePath, SceneMetaData* data)
{
    // TODO: persist scene metadata to disk (JSON or binary)
}

SceneMetaData loadScene(const char *filePath)
{
    SceneMetaData out = {0};
    // TODO: load scene metadata from disk and populate out
    return out;
}
