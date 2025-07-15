#pragma once
#include <druid.h>

#define NAME_MAX_SIZE 32 //32 chars

typedef struct{
    Arena arena;
    HashMap map;
    Mesh* meshBuffer;
    char** names;
    u32 count;
    u32 max;
    
}MeshMap;

extern MeshMap* meshMap;

MeshMap* createMeshMap(u32 meshCount);

bool addMesh(Mesh* mesh, const char* name);

Mesh* getMesh( const char* name);

const char* getMeshNameByIndex(MeshMap* map, u32 index);
void freeMeshMap(MeshMap* map);
