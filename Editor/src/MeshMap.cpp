#include "MeshMap.h"
#include "druid.h"
#include <cstdio>
#include <cstring>


u32 djb2Hash(const void* inStr, u32 capacity)
{
    const char* str = (const char*)inStr;
    u32 hash = 5381;
    i32 c;

    while((c = *str++))
    {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % capacity;
}


bool equals(const void* a, const void* b)
{
    return strcmp((const char*)a,(const char*)b) == 0;
}

MeshMap* createMeshMap(u32 meshCount)
{
    MeshMap* map = (MeshMap*)malloc(sizeof(MeshMap));

    //null check
    if(map == NULL)
    {
        printf("Mesh Map not allocated correctly\n");
        return NULL;
    }
     
    //allocate members
    map->max = meshCount;

    map->count = 0;
    
    if(!createMap(&map->map, meshCount, sizeof(char) * NAME_MAX_SIZE, sizeof(Mesh*), djb2Hash, equals))
    {
        printf("Mesh Hash map failed to create\n");
        return NULL;
    }

    map->meshBuffer = (Mesh*)malloc(sizeof(Mesh) * meshCount);
    
    if(map->meshBuffer == NULL)
    {
        printf("Mesh Buffer did not allocate\n");
        return NULL;
    }    

    map->names = (char**)malloc(sizeof(char*) * meshCount);
    if(map->names == NULL)
    {
        printf("Name Pointers did not allocate\n");
        return NULL;
    }
    //32 characters for a name 
    arenaCreate(&map->arena, NAME_MAX_SIZE * meshCount );

    return map;
}

 
bool addMesh(Mesh* mesh, const char* name)
{
    if (!mesh)
    {
        printf("Mesh is null, cannot add to map\n");
        return false;
    }

    if(meshMap->count >= meshMap->max)
    {
        printf("Mesh Map is full\n");
        return false;
    }
    u32 nameLen = strlen(name) +1;
    if(nameLen >= NAME_MAX_SIZE)
    {
        printf("Name given too big, needs to be %d characters\n",NAME_MAX_SIZE);
        return false;
    }
    char* nameCopy  = (char*)aalloc(&meshMap->arena,nameLen);
    strcpy(nameCopy,name); 
     // Store the mesh in the buffer
    meshMap->meshBuffer[meshMap->count] = *mesh;
   
    meshMap->names[meshMap->count] = nameCopy;

    Mesh* meshPtr = &meshMap->meshBuffer[meshMap->count]; 
    // Insert the string directly 
    bool result = insertMap(&meshMap->map, nameCopy,&meshPtr);
    
    if(result)
    {
        meshMap->count++;
        printf("Mesh added successfully at index %d\n", meshMap->count - 1);
    }
    return result;
}

Mesh* getMesh(const char* name)
{
    

    Mesh* result;
    if(findInMap(&meshMap->map, name, &result))
    {
        return result;
    }

    printf("No Mesh Found called: %s\n",name);
    return NULL;
}
const char* getMeshNameByIndex(MeshMap* map, u32 index)
{
    if(index >= map->count) return nullptr;
    return map->names[index];
}


void freeMeshMap(MeshMap* map)
{
    //free all data
    arenaDestroy(&map->arena);
    free(map->meshBuffer);
    destroyMap(&map->map);
    free(map);
}





