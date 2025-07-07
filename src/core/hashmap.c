// Hashmap implementation for Druid engine
// Provides basic hashmap functionality for engine subsystems
#include "../../include/druid.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

bool createMap(HashMap* map, u32 capacity, u32 keySize, u32 valueSize,
               u32 (*hashFunc)(const void*, u32),
               bool (*equalsFunc)(const void*, const void*))
{
    if (!map) return false;

    map->capacity = capacity;
    map->count = 0;
    map->keySize = keySize;
    map->valueSize = valueSize;
    map->hashFunc = hashFunc;
    map->equalsFunc = equalsFunc;

	map->arena = malloc(sizeof(Arena));
	if(!map->arena) return false;
	

    u32 arenaSize = capacity * (keySize + valueSize + sizeof(bool) + sizeof(Pair));
    if (!arenaCreate(map->arena, arenaSize)) {
        
	return false;
    }

    map->pairs = (Pair*)aalloc(map->arena, capacity * sizeof(Pair));
    if (!map->pairs) {
        arenaDestroy(map->arena);
        free(map->arena);
        return false;
    }

    for (u32 i = 0; i < capacity; i++) {
        map->pairs[i].key = aalloc(map->arena, keySize);
        if (!map->pairs[i].key) return false;

        map->pairs[i].value = aalloc(map->arena, valueSize);
        if (!map->pairs[i].value) return false;

        map->pairs[i].occupied = false;
    }

    return true;
}


void destroyMap(HashMap* map)
{
    if (!map) return;
    if (map->arena) {
        arenaDestroy(map->arena);
        free(map->arena);
        map->arena = NULL;
    }
}

bool insertMap(HashMap* map, const void* key, const void* value)
{
    if (!map || !key || !value) return false;
    if (map->count >= map->capacity) return false; // full map

    u32 hashIndex = map->hashFunc(key, map->capacity);

    for (u32 i = 0; i < map->capacity; i++) {
        u32 tryIndex = (hashIndex + i) % map->capacity;
        Pair* pair = &map->pairs[tryIndex];

        if (!pair->occupied) {
            memcpy(pair->key, key, map->keySize);
            memcpy(pair->value, value, map->valueSize);
            pair->occupied = true;
            map->count++;
            return true;
        }

        if (map->equalsFunc(pair->key, key)) {
            // Update existing value
            memcpy(pair->value, value, map->valueSize);
            return true;
        }
    }

    return false; // map full
}

bool findInMap(HashMap* map, const void* key, void* outValue)
{
    if (!map || !key || !outValue) return false;

    u32 hashIndex = map->hashFunc(key, map->capacity);

    for (u32 i = 0; i < map->capacity; i++) {
        u32 tryIndex = (hashIndex + i) % map->capacity;
        Pair* pair = &map->pairs[tryIndex];

        if (!pair->occupied) {
            return false; // not found
        }

        if (map->equalsFunc(pair->key, key)) {
            memcpy(outValue, pair->value, map->valueSize);
            return true;
        }
    }

    return false; // not found
}
