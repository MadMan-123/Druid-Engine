
#include "../../../include/druid.h"

//=====================================================================================================================
// Internal helpers

// total bytes per entity for fields of a given temperature
static u32 getEntitySizeForTemp(StructLayout *layout, FieldTemperature temp)
{
    u32 size = 0;
    for (u32 i = 0; i < layout->count; i++)
    {
        if (layout->fields[i].temperature == temp)
            size += layout->fields[i].size;
    }
    return size;
}

// wire chunk fields[] into the archetype-level flat blocks
static void setupChunkFieldPointers(EntityArena *chunk, StructLayout *layout,
                                    u32 chunkCapacity, u32 totalCapacity,
                                    u32 chunkIndex,
                                    u8 *hotBase, u8 *coldBase)
{
    // no hot base means all fields go to cold
    b8 noSplit = (hotBase == NULL);

    u64 hotFieldOffset  = 0;
    u64 coldFieldOffset = 0;

    for (u32 f = 0; f < layout->count; f++)
    {
        u64 fieldSize = layout->fields[f].size;
        u64 chunkByteOffset = (u64)chunkIndex * chunkCapacity * fieldSize;

        if (!noSplit && layout->fields[f].temperature == FIELD_TEMP_HOT)
        {
            chunk->fields[f] = hotBase + hotFieldOffset + chunkByteOffset;
            hotFieldOffset += fieldSize * totalCapacity;
        }
        else
        {
            chunk->fields[f] = coldBase ? (coldBase + coldFieldOffset + chunkByteOffset) : NULL;
            coldFieldOffset += fieldSize * totalCapacity;
        }
    }
}

static b8 mapPoolIndexToChunkLocal(const Archetype *arch, u32 poolIndex,
                                   u32 *outChunkIdx, u32 *outLocalIdx)
{
    if (!arch || arch->chunkCapacity == 0 || !outChunkIdx || !outLocalIdx)
        return false;

    *outChunkIdx = poolIndex / arch->chunkCapacity;
    *outLocalIdx = poolIndex % arch->chunkCapacity;
    return true;
}

//=====================================================================================================================
// Chunk query helpers

u32 archetypeEntityCount(Archetype *arch)
{
    return arch ? arch->cachedEntityCount : 0;
}

u32 archetypeChunkCount(Archetype *arch)
{
    return arch ? arch->activeChunkCount : 0;
}

void archetypeSetChunkSize(Archetype *arch, u32 sizeBytes)
{
    if (arch) arch->chunkSizeBytes = sizeBytes;
}

//=====================================================================================================================
// Hot/Cold contiguous data accessors

void *archetypeGetHotData(Archetype *arch)
{
    return arch ? arch->hotData : NULL;
}

void *archetypeGetColdData(Archetype *arch)
{
    return arch ? arch->coldData : NULL;
}

u32 archetypeGetHotEntitySize(Archetype *arch)
{
    return arch ? arch->hotEntitySize : 0;
}

u32 archetypeGetColdEntitySize(Archetype *arch)
{
    return arch ? arch->coldEntitySize : 0;
}

void archetypeSetTrigger(Archetype *arch, b8 isTrigger)
{
    if (arch) arch->isTrigger = isTrigger;
}

void archetypeSetCollisionCallbacks(Archetype *arch, CollisionCallbacks cbs)
{
    if (arch) arch->cbs = cbs;
}

//=====================================================================================================================
// createArchetype

b8 createArchetype(StructLayout *layout, u32 capacity, Archetype *outArchetype)
{
    if (outArchetype == NULL || layout == NULL || capacity == 0)
    {
        ERROR("createArchetype: Invalid parameters");
        return false;
    }

    outArchetype->layout = layout;
    outArchetype->capacity = capacity;
    outArchetype->id = 0; // caller sets to unique ID

    // enforce single-instance archetype
    if (FLAG_CHECK(outArchetype->flags, ARCH_SINGLE) && capacity > 1)
    {
        WARN("createArchetype: ARCH_SINGLE set, clamping capacity to 1");
        capacity = 1;
        outArchetype->capacity = 1;
    }

    // compute chunk capacity from target chunk size
    u32 entitySize = getEntitySize(layout);
    u32 chunkSize = outArchetype->chunkSizeBytes;
    if (chunkSize == 0) chunkSize = CHUNK_DEFAULT_SIZE;
    outArchetype->chunkSizeBytes = chunkSize;

    u32 rawPerChunk = chunkSize / entitySize;
    if (rawPerChunk == 0) rawPerChunk = 1;

    // align down to SIMD width
    u32 perChunk = (rawPerChunk / CHUNK_SIMD_ALIGN) * CHUNK_SIMD_ALIGN;
    if (perChunk == 0) perChunk = rawPerChunk;

    if (FLAG_CHECK(outArchetype->flags, ARCH_SINGLE))
    {
        perChunk = 1;
    }

    outArchetype->chunkCapacity = perChunk;

    // compute hot/cold entity sizes
    u32 hotEntitySize, coldEntitySize;
    if (FLAG_CHECK(outArchetype->flags, ARCH_NO_SPLIT))
    {
        hotEntitySize  = 0;
        coldEntitySize = getEntitySize(layout);
    }
    else
    {
        hotEntitySize  = getEntitySizeForTemp(layout, FIELD_TEMP_HOT);
        coldEntitySize = getEntitySizeForTemp(layout, FIELD_TEMP_COLD);
    }
    outArchetype->hotEntitySize  = hotEntitySize;
    outArchetype->coldEntitySize = coldEntitySize;

    // number of chunks needed
    u32 chunksNeeded = (capacity + perChunk - 1) / perChunk;
    if (chunksNeeded == 0) chunksNeeded = 1;

    u32 totalCapacity = chunksNeeded * perChunk;

    u64 hotTotal  = (u64)hotEntitySize  * totalCapacity;
    u64 coldTotal = (u64)coldEntitySize * totalCapacity;

    outArchetype->hotData  = NULL;
    outArchetype->coldData = NULL;

    if (hotTotal > 0)
    {
        outArchetype->hotData = dalloc((u32)hotTotal, MEM_TAG_ARCHETYPE);
        if (!outArchetype->hotData)
        {
            ERROR("createArchetype: Failed to allocate hot data block (%llu bytes)", hotTotal);
            return false;
        }
        memset(outArchetype->hotData, 0, hotTotal);
    }

    if (coldTotal > 0)
    {
        outArchetype->coldData = dalloc((u32)coldTotal, MEM_TAG_ARCHETYPE);
        if (!outArchetype->coldData)
        {
            ERROR("createArchetype: Failed to allocate cold data block (%llu bytes)", coldTotal);
            if (outArchetype->hotData)
                dfree(outArchetype->hotData, (u32)hotTotal, MEM_TAG_ARCHETYPE);
            return false;
        }
        memset(outArchetype->coldData, 0, coldTotal);
    }

    outArchetype->arena = (EntityArena *)dalloc(
        sizeof(EntityArena) * chunksNeeded, MEM_TAG_ARCHETYPE);

    if (!outArchetype->arena)
    {
        ERROR("createArchetype: Failed to allocate chunk array");
        if (outArchetype->hotData)
            dfree(outArchetype->hotData, (u32)hotTotal, MEM_TAG_ARCHETYPE);
        if (outArchetype->coldData)
            dfree(outArchetype->coldData, (u32)coldTotal, MEM_TAG_ARCHETYPE);
        return false;
    }

    // initialize each chunk as a view into the contiguous blocks
    for (u32 i = 0; i < chunksNeeded; i++)
    {
        EntityArena *chunk = &outArchetype->arena[i];
        memset(chunk, 0, sizeof(EntityArena));
        chunk->entityCount = perChunk;
        chunk->count = 0;
        chunk->layout = layout;
        chunk->entitySize = entitySize;
        chunk->fieldCount = layout->count;
        chunk->ownsData = false;
        chunk->data = NULL;

        chunk->fields = (void **)dalloc(sizeof(void *) * layout->count, MEM_TAG_ARCHETYPE);
        setupChunkFieldPointers(chunk, layout, perChunk, totalCapacity, i,
                                (u8 *)outArchetype->hotData,
                                (u8 *)outArchetype->coldData);
    }

    outArchetype->cachedEntityCount = 0;

    // initialize free-list for buffered archetypes (O(1) pool spawn)
    outArchetype->deadIndices = NULL;
    outArchetype->deadCount = 0;
    if (FLAG_CHECK(outArchetype->flags, ARCH_BUFFERED))
    {
        u32 poolCap = capacity;
        if (outArchetype->poolCapacity > 0)
            poolCap = outArchetype->poolCapacity;

        outArchetype->poolCapacity = poolCap;

        outArchetype->deadIndices = (u32 *)dalloc(
            sizeof(u32) * poolCap, MEM_TAG_ARCHETYPE);

        if (!outArchetype->deadIndices)
        {
            ERROR("createArchetype: Failed to allocate free-list for buffered archetype");
            // clean up
            for (u32 i = 0; i < chunksNeeded; i++)
                dfree(outArchetype->arena[i].fields, sizeof(void *) * layout->count, MEM_TAG_ARCHETYPE);
            dfree(outArchetype->arena, sizeof(EntityArena) * chunksNeeded, MEM_TAG_ARCHETYPE);
            if (outArchetype->hotData) dfree(outArchetype->hotData, (u32)hotTotal, MEM_TAG_ARCHETYPE);
            if (outArchetype->coldData) dfree(outArchetype->coldData, (u32)coldTotal, MEM_TAG_ARCHETYPE);
            return false;
        }

        // reverse fill so spawns return 0,1,2,...
        for (u32 i = 0; i < poolCap; i++)
        {
            outArchetype->deadIndices[poolCap - 1 - i] = i;
        }

        outArchetype->deadCount = poolCap;
    }

    outArchetype->arenaCount = chunksNeeded;
    outArchetype->activeChunkCount = 0;
    outArchetype->capacity = chunksNeeded * perChunk;

    INFO("createArchetype: '%s' - %u chunks x %u entities, hot=%u bytes/ent, cold=%u bytes/ent",
         layout->name, chunksNeeded, perChunk, hotEntitySize, coldEntitySize);

    return true;
}

//=====================================================================================================================
// destroyArchetype

b8 destroyArchetype(Archetype *arch)
{
    if (arch == NULL)
    {
        ERROR("destroyArchetype: Invalid archetype");
        return false;
    }

    // save counts before zeroing
    u32 savedArenaCount = arch->arenaCount;
    u32 savedChunkCap   = arch->chunkCapacity;

    if (arch->hotData)
    {
        u64 hotTotal = (u64)arch->hotEntitySize * savedChunkCap * savedArenaCount;
        dfree(arch->hotData, (u32)hotTotal, MEM_TAG_ARCHETYPE);
        arch->hotData = NULL;
    }

    if (arch->coldData)
    {
        u64 coldTotal = (u64)arch->coldEntitySize * savedChunkCap * savedArenaCount;
        dfree(arch->coldData, (u32)coldTotal, MEM_TAG_ARCHETYPE);
        arch->coldData = NULL;
    }

    if (arch->arena)
    {
        for (u32 i = 0; i < savedArenaCount; i++)
        {
            if (arch->arena[i].fields)
            {
                dfree(arch->arena[i].fields,
                      sizeof(void *) * arch->arena[i].fieldCount, MEM_TAG_ARCHETYPE);
                arch->arena[i].fields = NULL;
            }
        }
        dfree(arch->arena, sizeof(EntityArena) * savedArenaCount, MEM_TAG_ARCHETYPE);
        arch->arena = NULL;
        arch->arenaCount = 0;
        arch->activeChunkCount = 0;
    }

    if (arch->deadIndices)
    {
        u32 poolCap = (arch->poolCapacity > 0) ? arch->poolCapacity : arch->capacity;
        dfree(arch->deadIndices, sizeof(u32) * poolCap, MEM_TAG_ARCHETYPE);
        arch->deadIndices = NULL;
        arch->deadCount = 0;
    }

    return true;
}

//=====================================================================================================================
// Entity creation / removal (chunk-aware)

// grow archetype by reallocating contiguous blocks, copies field-by-field
static b8 archetypeGrow(Archetype *arch, u32 newChunkCount)
{
    u32 oldCount = arch->arenaCount;
    StructLayout *layout = arch->layout;
    u32 perChunk = arch->chunkCapacity;

    u32 oldTotalCap = oldCount * perChunk;
    u32 newTotalCap = newChunkCount * perChunk;

    u64 oldHotTotal  = (u64)arch->hotEntitySize  * oldTotalCap;
    u64 oldColdTotal = (u64)arch->coldEntitySize * oldTotalCap;
    u64 newHotTotal  = (u64)arch->hotEntitySize  * newTotalCap;
    u64 newColdTotal = (u64)arch->coldEntitySize * newTotalCap;

    // realloc hot block
    void *newHotData = arch->hotData;
    if (arch->hotEntitySize > 0)
    {
        newHotData = dalloc((u32)newHotTotal, MEM_TAG_ARCHETYPE);
        if (!newHotData) return false;
        memset(newHotData, 0, newHotTotal);
        if (arch->hotData)
        {
            u32 oldOff = 0, newOff = 0;
            for (u32 f = 0; f < layout->count; f++)
            {
                if (layout->fields[f].temperature != FIELD_TEMP_HOT) continue;
                u32 fs = layout->fields[f].size;
                memcpy((u8 *)newHotData + newOff, (u8 *)arch->hotData + oldOff, (u64)fs * oldTotalCap);
                oldOff += fs * oldTotalCap;
                newOff += fs * newTotalCap;
            }
            dfree(arch->hotData, (u32)oldHotTotal, MEM_TAG_ARCHETYPE);
        }
    }

    // realloc cold block
    void *newColdData = arch->coldData;
    if (arch->coldEntitySize > 0)
    {
        newColdData = dalloc((u32)newColdTotal, MEM_TAG_ARCHETYPE);
        if (!newColdData)
        {
            if (arch->hotEntitySize > 0 && newHotData != arch->hotData)
                dfree(newHotData, (u32)newHotTotal, MEM_TAG_ARCHETYPE);
            return false;
        }
        memset(newColdData, 0, newColdTotal);
        if (arch->coldData)
        {
            u32 oldOff = 0, newOff = 0;
            for (u32 f = 0; f < layout->count; f++)
            {
                if (layout->fields[f].temperature != FIELD_TEMP_COLD) continue;
                u32 fs = layout->fields[f].size;
                memcpy((u8 *)newColdData + newOff, (u8 *)arch->coldData + oldOff, (u64)fs * oldTotalCap);
                oldOff += fs * oldTotalCap;
                newOff += fs * newTotalCap;
            }
            dfree(arch->coldData, (u32)oldColdTotal, MEM_TAG_ARCHETYPE);
        }
    }

    arch->hotData  = newHotData;
    arch->coldData = newColdData;

    // grow arena metadata array
    u64 oldArenaSize = sizeof(EntityArena) * oldCount;
    u64 newArenaSize = sizeof(EntityArena) * newChunkCount;
    EntityArena *newArena = (EntityArena *)dalloc((u32)newArenaSize, MEM_TAG_ARCHETYPE);
    if (!newArena) return false;
    memset(newArena, 0, newArenaSize);
    memcpy(newArena, arch->arena, oldArenaSize);
    dfree(arch->arena, (u32)oldArenaSize, MEM_TAG_ARCHETYPE);
    arch->arena = newArena;

    // re-wire existing chunk field pointers
    for (u32 i = 0; i < oldCount; i++)
    {
        setupChunkFieldPointers(&arch->arena[i], layout, perChunk, newTotalCap, i,
                                (u8 *)arch->hotData, (u8 *)arch->coldData);
    }

    u32 entitySize = getEntitySize(layout);
    for (u32 i = oldCount; i < newChunkCount; i++)
    {
        EntityArena *chunk = &arch->arena[i];
        chunk->entityCount = perChunk;
        chunk->count = 0;
        chunk->layout = layout;
        chunk->entitySize = entitySize;
        chunk->fieldCount = layout->count;
        chunk->ownsData = false;
        chunk->data = NULL;

        chunk->fields = (void **)dalloc(sizeof(void *) * layout->count, MEM_TAG_ARCHETYPE);
        setupChunkFieldPointers(chunk, layout, perChunk, newTotalCap, i,
                                (u8 *)arch->hotData, (u8 *)arch->coldData);
    }

    arch->arenaCount = newChunkCount;
    arch->capacity = newTotalCap;

    return true;
}

u8 createEntityInArchetype(Archetype *arch, u64 *outEntity)
{
    if (arch == NULL || outEntity == NULL)
    {
        ERROR("createEntityInArchetype: Invalid parameters");
        return false;
    }

    if (arch->arenaCount == 0)
    {
        ERROR("createEntityInArchetype: No chunks in archetype");
        return false;
    }

    if (FLAG_CHECK(arch->flags, ARCH_SINGLE) && arch->arena[0].count >= 1)
    {
        ERROR("createEntityInArchetype: ARCH_SINGLE archetype already has an entity");
        return false;
    }

    // find first chunk with space
    u32 chunkIdx = (u32)-1;
    for (u32 i = 0; i < arch->arenaCount; i++)
    {
        if (arch->arena[i].count < arch->arena[i].entityCount)
        {
            chunkIdx = i;
            break;
        }
    }

    // all chunks full, grow by one
    if (chunkIdx == (u32)-1)
    {
        u32 newCount = arch->arenaCount + 1;
        if (!archetypeGrow(arch, newCount))
        {
            ERROR("createEntityInArchetype: Failed to grow archetype");
            return false;
        }
        chunkIdx = newCount - 1;
    }

    u32 localIndex = createEntity(&arch->arena[chunkIdx]);
    if (localIndex == (u32)-1)
    {
        ERROR("createEntityInArchetype: Failed to create entity in chunk");
        return false;
    }

    if (chunkIdx + 1 > arch->activeChunkCount)
        arch->activeChunkCount = chunkIdx + 1;

    arch->cachedEntityCount++;

    *outEntity = ENTITY_PACK(arch->id, chunkIdx, localIndex);
    return true;
}

b8 removeEntityFromArchetype(Archetype *arch, u32 arenaIndex, u32 index)
{
    if (arch == NULL)
    {
        ERROR("removeEntityFromArchetype: Invalid archetype");
        return false;
    }

    if (arch->arenaCount == 0)
    {
        ERROR("removeEntityFromArchetype: No chunks in archetype");
        return false;
    }

    if (arenaIndex >= arch->arenaCount)
    {
        ERROR("removeEntityFromArchetype: Invalid chunk index");
        return false;
    }

    b8 result = removeEntityFromArena(&arch->arena[arenaIndex], index);

    // update count cache, shrink active chunk count
    if (result)
    {
        arch->cachedEntityCount--;
        while (arch->activeChunkCount > 0
               && arch->arena[arch->activeChunkCount - 1].count == 0)
        {
            arch->activeChunkCount--;
        }
    }

    return result;
}

//=====================================================================================================================
// Field access

void **getArchetypeFields(Archetype *arch, u32 arenaIndex)
{
    if (arch == NULL || arch->arenaCount == 0)
    {
        ERROR("getArchetypeFields: Invalid archetype or no chunks");
        return NULL;
    }

    if (arenaIndex >= arch->arenaCount)
    {
        ERROR("getArchetypeFields: Invalid chunk index");
        return NULL;
    }

    return arch->arena[arenaIndex].fields;
}

//=====================================================================================================================
// Pool API for buffered archetypes (chunk-aware)

u32 archetypePoolSpawn(Archetype *arch)
{
    if (!arch || !FLAG_CHECK(arch->flags, ARCH_BUFFERED))
    {
        ERROR("archetypePoolSpawn: NULL archetype or not buffered");
        return (u32)-1;
    }

    // ========================================================================
    // HOT PATH (O(1)): Free-list based spawn for reusable dead entities
    if (arch->deadCount > 0)
    {
        u32 index = arch->deadIndices[--arch->deadCount];

        u32 chunkIdx = 0;
        u32 localIdx = 0;
        if (!mapPoolIndexToChunkLocal(arch, index, &chunkIdx, &localIdx))
        {
            ERROR("archetypePoolSpawn: Failed to map index from free-list");
            return (u32)-1;
        }

        // Sanity check (should never fail if free-list is properly maintained)
        if (chunkIdx >= arch->arenaCount)
        {
            ERROR("archetypePoolSpawn: Invalid index from free-list (corruption?)");
            return (u32)-1;
        }

        // Mark entity alive
        b8 *alive = (b8 *)arch->arena[chunkIdx].fields[0];
        alive[localIdx] = true;

        // Buffered archetypes use a stable pool index. Keep chunk count as
        // a high-water mark so chunk iteration can reach this slot.
        if (localIdx + 1 > arch->arena[chunkIdx].count)
            arch->arena[chunkIdx].count = localIdx + 1;

        // Update active chunk count if needed
        if (chunkIdx + 1 > arch->activeChunkCount)
            arch->activeChunkCount = chunkIdx + 1;

        arch->cachedEntityCount++;

        return index;
    }

    // ========================================================================
    // SLOW PATH: Growth (when pre-allocated pool exhausted)
    if (arch->poolCapacity == 0 || archetypeEntityCount(arch) < arch->poolCapacity)
    {
        u32 newCount = arch->arenaCount + 1;
        if (!archetypeGrow(arch, newCount))
        {
            ERROR("archetypePoolSpawn: Failed to grow archetype");
            return (u32)-1;
        }

        u32 c = newCount - 1;
        arch->arena[c].count = 1;
        arch->activeChunkCount = newCount;

        arch->cachedEntityCount++;

        b8 *alive = (b8 *)arch->arena[c].fields[0];
        alive[0] = true;
        return c * arch->chunkCapacity + 0;
    }

    WARN("archetypePoolSpawn: pool is full (capacity %u)", arch->poolCapacity);
    return (u32)-1;
}

b8 archetypePoolSpawnFields(Archetype *arch, u32 *outPoolIdx, u32 *outLocalIdx, void ***outFields)
{
    u32 poolIdx = archetypePoolSpawn(arch);
    if (poolIdx == (u32)-1) return false;

    u32 chunkIdx = poolIdx / arch->chunkCapacity;
    void **fields = getArchetypeFields(arch, chunkIdx);
    if (!fields)
    {
        archetypePoolDespawn(arch, poolIdx);
        return false;
    }

    if (outPoolIdx)  *outPoolIdx  = poolIdx;
    if (outLocalIdx) *outLocalIdx = poolIdx % arch->chunkCapacity;
    if (outFields)   *outFields   = fields;
    return true;
}

void archetypePoolDespawn(Archetype *arch, u32 poolIndex)
{
    if (!arch || !FLAG_CHECK(arch->flags, ARCH_BUFFERED))
    {
        ERROR("archetypePoolDespawn: NULL archetype or not buffered");
        return;
    }

    u32 chunkIdx = poolIndex / arch->chunkCapacity;
    u32 localIdx = poolIndex % arch->chunkCapacity;

    if (chunkIdx >= arch->arenaCount) return;

    b8 *alive = (b8 *)arch->arena[chunkIdx].fields[0];
    if (localIdx < arch->arena[chunkIdx].count)
    {
        if (!alive[localIdx]) return;

        alive[localIdx] = false;

        arch->cachedEntityCount--;

        // Push index back onto free-list stack for reuse (O(1))
        if (arch->deadIndices && arch->deadCount < arch->poolCapacity)
        {
            arch->deadIndices[arch->deadCount++] = poolIndex;
        }
    }
}

b8 archetypePoolIsAlive(Archetype *arch, u32 poolIndex)
{
    if (!arch || !FLAG_CHECK(arch->flags, ARCH_BUFFERED)) return false;

    u32 chunkIdx = poolIndex / arch->chunkCapacity;
    u32 localIdx = poolIndex % arch->chunkCapacity;

    if (chunkIdx >= arch->arenaCount) return false;
    if (localIdx >= arch->arena[chunkIdx].count) return false;

    b8 *alive = (b8 *)arch->arena[chunkIdx].fields[0];
    return alive[localIdx];
}

ArchetypeSpawnData archetypeSpawnIn(Archetype *arch)
{
    ArchetypeSpawnData result;
    result.fields = NULL;
    result.chunkIdx = (u32)-1;
    result.localIdx = (u32)-1;
    result.poolIdx = (u32)-1;

    if (!arch || !FLAG_CHECK(arch->flags, ARCH_BUFFERED))
    {
        ERROR("archetypeSpawnIn: NULL archetype or not buffered");
        return result;
    }

    u32 poolIdx = archetypePoolSpawn(arch);
    if (poolIdx == (u32)-1)
        return result;

    u32 chunkIdx = 0;
    u32 localIdx = 0;
    if (!mapPoolIndexToChunkLocal(arch, poolIdx, &chunkIdx, &localIdx))
    {
        ERROR("archetypeSpawnIn: Failed to map pool index");
        return result;
    }

    if (chunkIdx >= arch->arenaCount)
    {
        ERROR("archetypeSpawnIn: Resolved chunk index out of bounds");
        return result;
    }

    if (localIdx >= arch->arena[chunkIdx].count)
    {
        ERROR("archetypeSpawnIn: Resolved local index out of bounds");
        return result;
    }

    result.fields = getArchetypeFields(arch, chunkIdx);
    if (!result.fields)
        return result;

    result.chunkIdx = chunkIdx;
    result.localIdx = localIdx;
    result.poolIdx = poolIdx;
    return result;
}

ArchetypeSpawnData archetypeSpawnInWithCallback(Archetype *arch,
                                                ArchetypeInitCallback callback,
                                                void *userdata)
{
    ArchetypeSpawnData result = archetypeSpawnIn(arch);

    if (result.fields && callback)
    {
        callback(&result, userdata);
    }

    return result;
}
