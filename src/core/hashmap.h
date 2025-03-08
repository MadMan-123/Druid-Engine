#pragma once
#include "arena.h"
#include "../defines.h"

//Thank you Jacob Sorber for your video on hash tables in C

#define MAX_NAME 256

typedef struct{
	char name[MAX_NAME];
	void* data;
}Pair;

typedef struct{
	size_t mapSize;
	Pair* pairs;
	Arena* arena;
}HashMap;


DAPI bool createHashMap(HashMap* map, size_t mapSize);
DAPI unsigned int hash(char* name,size_t mapSize);
DAPI void printMap(HashMap* map);
DAPI bool insertMap(HashMap* map);
DAPI void cleanMap(HashMap* map);
