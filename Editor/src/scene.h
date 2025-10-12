#pragma once
#include <druid.h>
// some managment of Entity Arenas
// Entity Layout Descriptors including metadata on
// Scene Creation
//       -Scene Entities

#define MAX_NAME_SIZE 16
#define MAX_MESH_NAME_SIZE 32 // this should be managed in somewhere else

// Centralized transform fields macro so the transform representation can be
// swapped in one place (for example: Vec3/Vec4 vs separate floats or SoA layout)
#define TRANSFORM_FIELDS \
    FIELD(Vec3, position),  \
    FIELD(Vec4, rotation),  \
    FIELD(Vec3, scale)

// Forward declaration only - definition is in scene.cpp
//extern FieldInfo SceneEntity_fields[];
//extern StructLayout SceneEntity;
// Define the SceneEntity archetype
DEFINE_ARCHETYPE(SceneEntity, 
    TRANSFORM_FIELDS, 
    FIELD(bool, isActive), 
    FIELD(char[MAX_NAME_SIZE], name), 
    FIELD(u32, modelID),
    FIELD(u32, materialID),
    FIELD(u32, shaderHandle),
);





typedef struct {
    Archetype *archetypes;
    u32 archetypeCount;
    //metadata for scene entities

}Scene;

typedef struct{
    u32 entityCount;
    Archetype savedEntities;
}SceneMetaData;

void saveScene(const char *filePath, SceneMetaData* data);
SceneMetaData bakeScene(Scene* scene);
Scene loadScene(const char *filePath);


typedef struct{
    Scene* scenes;
    u32 sceneCount;
    u32 currentScene;
}SceneManager;

SceneManager* createSceneManager(u32 sceneCount);
void destroySceneManager(SceneManager* manager);
void addScene(SceneManager* manager, Scene* scene);
void switchScene(SceneManager* manager, u32 sceneIndex);



