#pragma once
#include <druid.h>
// some managment of Entity Arenas
// Entity Layout Descriptors including metadata on
// Scene Creation
//       -Scene Entities

#define MAX_NAME_SIZE 16
#define MAX_MESH_NAME_SIZE 32 // this should be managed in somewhere else

// Forward declaration only - definition is in scene.cpp
//extern FieldInfo SceneEntity_fields[];
//extern StructLayout SceneEntity;
// Define the SceneEntity archetype
DEFINE_ARCHETYPE(SceneEntity, 
    FIELD(Vec3, position), 
    FIELD(Vec4, rotation),
    FIELD(Vec3, scale), 
    FIELD(bool, isActive), 
    FIELD(char[MAX_NAME_SIZE], name), 
    FIELD(u32, modelID),
);
