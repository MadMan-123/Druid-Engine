#include "../../include/druid.h"

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
	for(int i = 0; i < layout->count; i++)
	{
		size += layout->fields[i].size;
	}

	return size;
}


//create Entity Arena
EntityArena* createEntityArena(StructLayout* layout, u32 entityCount)
{
	EntityArena* arena = (EntityArena*)malloc(sizeof(EntityArena));

	//get total size of the arena
	u32 entitySize = getEntitySize(layout);
	const u32 total = entitySize * entityCount;
	
	//set inital meta data
	arena->entityCount = entityCount;
	arena->count = 0;
	arena->layout = layout;
	
	//allocate the field pointers
	arena->fields = (void**)malloc(sizeof(void*) * layout->count);

	//allocate memory
	arena->data = malloc(total);

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

	//return the arena
	return arena;
}

//free the arena
bool freeEntityArena(EntityArena* arena)
{
	free(arena->fields);
	//free the whole thing
	free(arena->data);

	//free the entity arena itself
	free(arena);
}

//allocate an enitity
u32 createEntity(EntityArena* arena)
{
	if(arena->count >= arena->entityCount)
	{
		printf("Arena Filled\n");
		return 0;
	}
	u32 id = arena->count;

	arena->count++;

	return id;
}


