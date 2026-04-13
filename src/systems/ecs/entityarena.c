#include "../../../include/druid.h"

void printEntityArena(EntityArena* arena)
{
	for(u32 x = 0; x < arena->layout->count; x++)
	{
		printf("%s, %i\n",arena->layout->fields[x].name, arena->layout->fields[x].size);
	}
}

u32 getEntitySize(StructLayout* layout)
{
	u32 size = 0;
	for(i32 i = 0; i < layout->count; i++)
	{
		size += layout->fields[i].size;
	}

	return size;
}


//=====================================================================================================================
// Chunked allocation - create a single fixed-size chunk
//=====================================================================================================================

EntityArena createEntityArenaChunk(StructLayout *layout, u32 chunkCapacity)
{
    EntityArena ea = {0};
    ea.entityCount = chunkCapacity;
    ea.count = 0;
    ea.layout = layout;
    ea.ownsData = true;

    u32 entitySize = getEntitySize(layout);
    ea.entitySize = entitySize;
    ea.fieldCount = layout->count;
    u32 total = entitySize * chunkCapacity;

    ea.fields = (void **)dalloc(sizeof(void *) * layout->count, MEM_TAG_ARCHETYPE);
    ea.data = dalloc(total, MEM_TAG_ARCHETYPE);

    u8 *base = (u8 *)ea.data;
    u32 offset = 0;
    for (u32 i = 0; i < layout->count; i++)
    {
        ea.fields[i] = base + offset;
        offset += layout->fields[i].size * chunkCapacity;
    }

    return ea;
}

void freeEntityArenaChunk(EntityArena *chunk)
{
    if (!chunk) return;

    u32 entitySize = chunk->entitySize;
    u32 fieldCount = chunk->fieldCount;
    if (entitySize == 0 || fieldCount == 0) return;

    if (chunk->fields)
    {
        dfree(chunk->fields, sizeof(void *) * fieldCount, MEM_TAG_ARCHETYPE);
        chunk->fields = NULL;
    }

    // only free data if chunk owns it
    if (chunk->ownsData && chunk->data)
    {
        u32 total = entitySize * chunk->entityCount;
        dfree(chunk->data, total, MEM_TAG_ARCHETYPE);
        chunk->data = NULL;
    }
}

//=====================================================================================================================
// Legacy - create Entity Arena (single big allocation, splits if > 64MB)
//=====================================================================================================================

EntityArena* createEntityArena(StructLayout* layout, u32 entityCount, u32* outArenas)
{
	const u32 ARENA_MAX_SIZE = 67108864; // 64MB max for now

	u32 entitySize = getEntitySize(layout);
	const u32 total = entitySize * entityCount;

	EntityArena* arena = NULL;
	if(total > ARENA_MAX_SIZE)
	{
		f32 arenasNeeded = total / ARENA_MAX_SIZE;
		DEBUG("Entity Arena requested size %d exceeds max of %d\nnow allocating %d arenas", total, ARENA_MAX_SIZE, (u32)arenasNeeded);

		arena = (EntityArena*)dalloc(sizeof(EntityArena) * (u32)arenasNeeded, MEM_TAG_ARCHETYPE);
		for(u32 i = 0; i < (u32)arenasNeeded; i++)
		{
			arena[i].entityCount = entityCount;
			arena[i].count = 0;
			arena[i].entitySize = entitySize;
			arena[i].fieldCount = layout->count;
			arena[i].layout = layout;
			arena[i].ownsData = true;

			arena[i].fields = (void**)dalloc(sizeof(void*) * layout->count, MEM_TAG_ARCHETYPE);

			//allocate memory (zero-init so inactive entities have sane defaults)
			arena[i].data = dalloc(ARENA_MAX_SIZE, MEM_TAG_ARCHETYPE);
			memset(arena[i].data, 0, ARENA_MAX_SIZE);

			u8* base = (u8*)arena[i].data;
			u32 offset = 0;
			for(u32 j = 0; j < layout->count; j++)
			{
				arena[i].fields[j] = base + offset;
				offset += layout->fields[j].size * entityCount;
			}
		}

		*outArenas = (u32)arenasNeeded;
	}
	else
	{
		DEBUG("Entity Arena requested size %d is within max of %d\n", total, ARENA_MAX_SIZE);
		arena = (EntityArena*)dalloc(sizeof(EntityArena), MEM_TAG_ARCHETYPE);

		arena->entityCount = entityCount;
		arena->count = 0;
		arena->entitySize = entitySize;
		arena->fieldCount = layout->count;
		arena->layout = layout;
		arena->ownsData = true;

		arena->fields = (void**)dalloc(sizeof(void*) * layout->count, MEM_TAG_ARCHETYPE);

		//allocate memory (zero-init so inactive entities have sane defaults)
		arena->data = dalloc(total, MEM_TAG_ARCHETYPE);
		memset(arena->data, 0, total);
		u8* base = (u8*)arena->data;
		u32 offset = 0;

		for(u32 i = 0; i < layout->count; i++)
		{
			arena->fields[i] = base + offset;
			offset += layout->fields[i].size * entityCount;
		}

		*outArenas = 1;
	}


	return arena;

}

b8 freeEntityArena(EntityArena* arena, u32 arenaCount)
{
	for(u32 i = 0; i < arenaCount; i++)
	{
		u32 entitySize = arena[i].entitySize;
		u32 total = entitySize * arena[i].entityCount;
		dfree(arena[i].fields, sizeof(void*) * arena[i].fieldCount, MEM_TAG_ARCHETYPE);
		dfree(arena[i].data, total, MEM_TAG_ARCHETYPE);
	}

	dfree(arena, sizeof(EntityArena) * arenaCount, MEM_TAG_ARCHETYPE);
	return true;
}

// allocate an entity, returns 0-based index or (u32)-1 on failure
u32 createEntity(EntityArena* arena)
{
	if(arena->count >= arena->entityCount)
	{
		WARN("Arena Filled\n");
		return (u32)-1;
	}
	u32 id = arena->count;

	arena->count++;

	return id;
}

// Remove entity from arena by index (0-based). Returns true on success.
b8 removeEntityFromArena(EntityArena* arena, u32 index)
{
	if(arena == NULL)
	{
		ERROR("removeEntityFromArena: arena is NULL");
		return false;
	}

	if(index >= arena->entityCount)
	{
		ERROR("removeEntityFromArena: index out of bounds");
		return false;
	}

	if(arena->count == 0)
	{
		WARN("removeEntityFromArena: arena empty");
		return false;
	}

	// swap-remove: copy last element into the removed slot per-field (SoA layout)
	u32 lastIndex = arena->count - 1;
	if(index != lastIndex)
	{
		for(u32 i = 0; i < arena->layout->count; i++)
		{
			u32 fieldSize = arena->layout->fields[i].size;
			u8 *field = (u8 *)arena->fields[i];
			memcpy(field + index * fieldSize, field + lastIndex * fieldSize, fieldSize);
		}
	}

	arena->count--;
	return true;
}


