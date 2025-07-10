#pragma once
#include <druid.h>
    //some managment of Entity Arenas
    //Entity Layout Descriptors including metadata on 
    //Scene Creation 
    //      -Scene Entities

DEFINE_ARCHETYPE(SceneEntity,
    FIELD(Vec3,position),
    FIELD(Vec4,rotation),
    FIELD(Vec3,scale),
    FIELD(bool,isActive)
);

typedef struct{
    
}Scene;


