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


//create Entity Arena
EntityArena* createEntityArena(StructLayout* layout, u32 entityCount, u32* outArenas)
{
	const u32 ARENA_MAX_SIZE = 67108864; // 64MB max for now

	//get total size of the arena
	u32 entitySize = getEntitySize(layout);
	const u32 total = entitySize * entityCount;

	EntityArena* arena = NULL;	//set inital meta data
	//work out if we need multple arenas
	if(total > ARENA_MAX_SIZE)
	{
		f32 arenasNeeded = total / ARENA_MAX_SIZE;
		DEBUG("Entity Arena requested size %d exceeds max of %d\nnow allocating %d arenas", total, ARENA_MAX_SIZE, (u32)arenasNeeded);
		
		arena = (EntityArena*)malloc(sizeof(EntityArena) * (u32)arenasNeeded);
		//initialize each arena
		for(u32 i = 0; i < (u32)arenasNeeded; i++)
		{
			arena[i].entityCount = entityCount;
			arena[i].count = 0;
			arena[i].layout = layout;

			//allocate the field pointers
			arena[i].fields = (void**)malloc(sizeof(void*) * layout->count);

			//allocate memory (zero-init so inactive entities have sane defaults)
			arena[i].data = malloc(ARENA_MAX_SIZE);
			memset(arena[i].data, 0, ARENA_MAX_SIZE);

			//base of memory arena
			u8* base = (u8*)arena[i].data;
			//track the offset to adjust pointers by
			u32 offset = 0;
			//set the field pointers
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
		arena = (EntityArena*)malloc(sizeof(EntityArena));
		
		arena->entityCount = entityCount;
		arena->count = 0;
		arena->layout = layout;
	
		//allocate the field pointers
		arena->fields = (void**)malloc(sizeof(void*) * layout->count);

		//allocate memory (zero-init so inactive entities have sane defaults)
		arena->data = malloc(total);
		memset(arena->data, 0, total);
		//base of memory arena
		u8* base = (u8*)arena->data;
		//track the offset to adjust pointers by
		u32 offset = 0;

		//set the field pointers
		for(u32 i = 0; i < layout->count; i++)
		{
			arena->fields[i] = base + offset;
			offset += layout->fields[i].size * entityCount; 	
		}

		*outArenas = 1;
	}


	return arena;
	
}

//free the arena
b8 freeEntityArena(EntityArena* arena, u32 arenaCount)
{
	//free each arena if there are multiple
	for(u32 i = 0; i < arenaCount; i++)
	{
		free(arena[i].fields);
		//free the whole thing
		free(arena[i].data);
	}

	//free the entity arena itself
	free(arena);
	return true;
}

//allocate an enitity
u32 createEntity(EntityArena* arena)
{
	
	if(arena->count >= arena->entityCount)
	{
		WARN("Arena Filled\n");
		return 0;
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

	// move last element into the removed slot for each field to keep packed layout
	u32 lastIndex = arena->count - 1;
	if(index != lastIndex)
	{
		u8 *base = (u8*)arena->data;
		u32 entitySize = 0;
		// compute total entity size from layout
		for(u32 i = 0; i < arena->layout->count; i++)
			entitySize += arena->layout->fields[i].size;

		// copy data from lastIndex to index for the entire entity block
		u8 *dst = base + (index * entitySize);
		u8 *src = base + (lastIndex * entitySize);
		memcpy(dst, src, entitySize);
	}

	arena->count--;
	return true;
}


