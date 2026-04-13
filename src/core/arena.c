#include "../../include/druid.h"

b8 arenaCreate(Arena *arena, u64 maxSize)
{
    if (!arena)
        return false;
    arena->size = maxSize;
    arena->used = 0;
    arena->data = dalloc(maxSize, MEM_TAG_ARENA);
    return arena->data != NULL;
}

void arenaDestroy(Arena *arena)
{
    if (!arena)
        return;
    dfree(arena->data, arena->size, MEM_TAG_ARENA);
    arena->data = NULL;
}

void *aalloc(Arena *arena, u64 size)
{
    if (!arena)
        return NULL;

    if (arena->used + size > arena->size)
        return NULL;
    void *ptr = (c8 *)(arena->data) + arena->used;
    arena->used += size;
    return ptr;
}
