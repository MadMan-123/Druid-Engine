#pragma once
#include <druid.h>
    //some managment of Entity Arenas
    //Entity Layout Descriptors including metadata on 
    //Scene Creation 
    //      -Scene Entities

#define MAX_NAME_SIZE 16
#define MAX_MESH_NAME_SIZE 32 //this should be managed in somewhere else

DEFINE_ARCHETYPE(SceneEntity,
    FIELD(Vec3,position),
    FIELD(Vec4,rotation),
    FIELD(Vec3,scale),
    FIELD(bool,isActive),

    FIELD(char[MAX_NAME_SIZE],name),
    FIELD(char[MAX_MESH_NAME_SIZE], meshName)
);

typedef struct{
    
}Scene;


