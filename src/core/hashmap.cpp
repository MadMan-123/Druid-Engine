#include "hashmap.h"
#include <string.h>
#include <cstdio>

bool createHashMap(HashMap* map, size_t mapSize)
{
	
	//allocate the map
	map = (HashMap*)malloc(sizeof(HashMap));
	//craete the buffer to work with
	if(!arenaCreate(map->arena,sizeof(Pair) * mapSize)) return false;
	//set metadata
	map->mapSize = mapSize;
	
	
	map->pairs = (Pair*)(aalloc(map->arena,sizeof(Pair) * mapSize));

	return true;
}

unsigned int hash(char* name,size_t mapSize)
{
	//get the length of the name
	int len = strnlen(name,MAX_NAME);
	
	//setting a uint cache 
	//TODO: add u32 definitions
	unsigned int cache = 0;
	for(int i = 0; i < len; i++)
	{
		//add the ascii value to the cache
		cache += name[i];
		//multiply the cache by the name char ascii value and then keep it in the range of the map size
		cache = (cache * name[i]) % mapSize;
	}


	return cache;
}


void printMap(HashMap* map)
{
	//loop through all elements and print out the table element
	for(int i = 0; i < map->mapSize; i++)
	{
		if(&map->pairs[i] == nullptr)
		{
			printf("\t%i\t---", i);
		}
		else
		{
			printf("\t%i\t%s\n",i,map->pairs[i].name);
		}
	}
}



bool insertMap(HashMap* map, Pair* pair)
{
	//is pair valid
	if(pair == nullptr) return false;
	//get an index to work with 
	int index = hash(pair->name,map->mapSize);
	//if there is something in the spot
	if(&(map->pairs[index]) != nullptr) return false;
	//set the value
	map->pairs[index] = *pair;
	//return
	return true;

}

void cleanMap(HashMap* map)
{
	arenaDestroy(map->arena);
	free(map);	
}
