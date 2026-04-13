#include "../../include/druid.h"
#include <stdlib.h>
#include <string.h>

b8 bufferCreate(Buffer *buf, u32 elemSize, u32 capacity)
{
    if (!buf || elemSize == 0 || capacity == 0)
    {
        ERROR("bufferCreate: invalid parameters");
        return false;
    }

    buf->data      = dalloc((u64)capacity * elemSize, MEM_TAG_BUFFER);
    buf->occupied  = (b8 *)dalloc((u64)capacity * sizeof(b8), MEM_TAG_BUFFER);
    buf->freeStack = (u32 *)dalloc((u64)capacity * sizeof(u32), MEM_TAG_BUFFER);
    buf->elemSize  = elemSize;
    buf->capacity  = capacity;
    buf->count     = 0;
    buf->freeCount = capacity;  // all slots are free initially

    if (!buf->data || !buf->occupied || !buf->freeStack)
    {
        ERROR("bufferCreate: allocation failed");
        dfree(buf->data, (u64)capacity * elemSize, MEM_TAG_BUFFER);
        dfree(buf->occupied, (u64)capacity * sizeof(b8), MEM_TAG_BUFFER);
        dfree(buf->freeStack, (u64)capacity * sizeof(u32), MEM_TAG_BUFFER);
        memset(buf, 0, sizeof(Buffer));
        return false;
    }

    // Initialize freeStack with all indices (in reverse order for natural LIFO)
    for (u32 i = 0; i < capacity; i++)
    {
        buf->freeStack[i] = i;
    }

    return true;
}

u32 bufferAcquire(Buffer *buf)
{
    if (!buf || !buf->data) return (u32)-1;

    if (buf->freeCount == 0)
    {
        ERROR("bufferAcquire: no free slots (capacity %u)", buf->capacity);
        return (u32)-1;
    }

    u32 index = buf->freeStack[--buf->freeCount];

    buf->occupied[index] = true;
    buf->count++;

    // Zero the slot so the caller gets a clean element
    memset((u8 *)buf->data + (size_t)index * buf->elemSize, 0, buf->elemSize);

    return index;
}

void bufferRelease(Buffer *buf, u32 index)
{
    if (!buf || index >= buf->capacity) return;
    if (!buf->occupied[index]) return;  // Guard against double-free

    memset((u8 *)buf->data + (size_t)index * buf->elemSize, 0, buf->elemSize);

    buf->occupied[index] = false;
    buf->count--;

    buf->freeStack[buf->freeCount++] = index;
}

void *bufferGet(Buffer *buf, u32 index)
{
    if (!buf || !buf->data || index >= buf->capacity)
        return NULL;
    return (u8 *)buf->data + (size_t)index * buf->elemSize;
}

b8 bufferIsOccupied(Buffer *buf, u32 index)
{
    if (!buf || index >= buf->capacity) return false;
    return buf->occupied[index];
}

void bufferDestroy(Buffer *buf)
{
    if (!buf) return;

    dfree(buf->data, (u64)buf->capacity * buf->elemSize, MEM_TAG_BUFFER);
    dfree(buf->occupied, (u64)buf->capacity * sizeof(b8), MEM_TAG_BUFFER);
    dfree(buf->freeStack, (u64)buf->capacity * sizeof(u32), MEM_TAG_BUFFER);
    memset(buf, 0, sizeof(Buffer));
}
