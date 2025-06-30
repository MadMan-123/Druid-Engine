    
#include "../../include/druid.h"

bool arenaCreate(Arena* arena, u32 maxSize)
{
    if (!arena) return false;

    arena->size = maxSize;
    arena->used = 0;
    arena->data = malloc(maxSize);
    return arena->data != NULL;
}

void arenaDestroy(Arena* arena)
{
    if (!arena) return;
    free(arena->data);
    arena->data = NULL;
}


void* aalloc(Arena* arena, size_t size)
{
    if (!arena) return NULL;

    //check if there is enough space in the arena
    if(arena->used + size > arena->size) return NULL;
    //allocate memory
    void* ptr = (char*)(arena->data) + arena->used;
    arena->used += size;
    //return pointer to allocated memory    
    return ptr;
}


