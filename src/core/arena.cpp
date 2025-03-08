    
#include "arena.h"

bool arenaCreate(Arena* arena, size_t maxSize)
{
    //set initial data for arena
    arena->size = maxSize;
    arena->used = 0;
    //allocate memory for arena
    arena->data = malloc(arena->size);
    return (arena->data == nullptr);
}

void* aalloc(Arena* arena, size_t size)
{
    //check if there is enough space in the arena
    if(arena->used + size > arena->size) return nullptr;
    //allocate memory
    void* ptr = (char*)(arena->data) + arena->used;
    arena->used += size;
    //return pointer to allocated memory    
    return ptr;
}

void arenaDestroy(Arena* arena)
{
    //free memory
    free(arena->data);
    //set data to null
    arena->data = nullptr;
    
}
