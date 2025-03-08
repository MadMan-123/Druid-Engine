#pragma once
#include "../defines.h"

typedef struct{
    void* data;
    size_t size;
    size_t used;
}Arena;

DAPI bool arenaCreate(Arena* arena, size_t maxSize);
DAPI void* aalloc(Arena* arena, size_t size);
DAPI void arenaDestroy(Arena* arena);

