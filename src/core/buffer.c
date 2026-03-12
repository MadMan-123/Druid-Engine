#include "../../include/druid.h"
#include <stdlib.h>
#include <string.h>

//=====================================================================================================================
// Buffer — generic slot-based object buffer
//
// Heap-allocates `capacity * elemSize` bytes and a parallel b8 occupancy array.
// Acquire returns the first free index; release zeros the slot and frees it.
//=====================================================================================================================

b8 bufferCreate(Buffer *buf, u32 elemSize, u32 capacity)
{
    if (!buf || elemSize == 0 || capacity == 0)
    {
        ERROR("bufferCreate: invalid parameters");
        return false;
    }

    buf->data     = calloc(capacity, elemSize);
    buf->occupied = (b8 *)calloc(capacity, sizeof(b8));
    buf->elemSize = elemSize;
    buf->capacity = capacity;
    buf->count    = 0;

    if (!buf->data || !buf->occupied)
    {
        ERROR("bufferCreate: allocation failed");
        free(buf->data);
        free(buf->occupied);
        memset(buf, 0, sizeof(Buffer));
        return false;
    }

    return true;
}

u32 bufferAcquire(Buffer *buf)
{
    if (!buf || !buf->data) return (u32)-1;

    for (u32 i = 0; i < buf->capacity; i++)
    {
        if (!buf->occupied[i])
        {
            buf->occupied[i] = true;
            buf->count++;
            // zero the slot so the caller gets a clean element
            memset((u8 *)buf->data + (size_t)i * buf->elemSize, 0, buf->elemSize);
            return i;
        }
    }

    ERROR("bufferAcquire: no free slots (capacity %u)", buf->capacity);
    return (u32)-1;
}

void bufferRelease(Buffer *buf, u32 index)
{
    if (!buf || index >= buf->capacity) return;
    if (!buf->occupied[index]) return;

    memset((u8 *)buf->data + (size_t)index * buf->elemSize, 0, buf->elemSize);
    buf->occupied[index] = false;
    buf->count--;
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

    free(buf->data);
    free(buf->occupied);
    memset(buf, 0, sizeof(Buffer));
}
