#pragma once
#include <druid.h>

#define NAME_MAX_SIZE 32 //32 chars

typedef struct{
    Arena arena;
    HashMap map;
    Mesh* meshBuffer;
    u32 count;
    u32 max;
    
}MeshMap;

extern MeshMap* meshMap;

MeshMap* createMeshMap(u32 meshCount);

bool addMesh(Mesh* mesh, const char* name);

Mesh* getMesh(MeshMap* map, const char* name);

void freeMeshMap(MeshMap* map);
