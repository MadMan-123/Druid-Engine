
#include "../../../include/druid.h"

//create archetype
b8 createArchetype(StructLayout *layout, u32 capacity, Archetype *outArchetype)
{
    if(outArchetype == NULL || layout == NULL || capacity == 0)
    {
        ERROR("createArchetype: Invalid parameters");
        
        return false;
    }

    //fill out the entity arenas
    outArchetype->layout = layout;
    outArchetype->capacity = capacity;
    outArchetype->id = 0; // This should be set by the caller to a unique ID

    // enforce single-instance archetype: cap at 1 entity
    // (isSingle must be set by the caller BEFORE calling createArchetype)
    if(outArchetype->isSingle && capacity > 1)
    {
        WARN("createArchetype: isSingle set, clamping capacity to 1");
        capacity = 1;
        outArchetype->capacity = 1;
    }

    //create arenas for the archetype
    outArchetype->arena = createEntityArena(layout, capacity, &outArchetype->arenaCount);    

    if(outArchetype->arena == NULL)
    {
        ERROR("createArchetype: Failed to create entity arena");
        return false;
    }
    return true;
}

//destroy archetype
b8 destroyArchetype(Archetype *arch)
{
    if(arch == NULL)
    {
        ERROR("destroyArchetype: Invalid archetype");
        return false;
    }

    //free the entity arena
    if(arch->arena)
    {
        freeEntityArena(arch->arena, arch->arenaCount);
        arch->arena = NULL;
        arch->arenaCount = 0;
    }

    return true;
}

// create an entity in the given archetype
u8 createEntityInArchetype(Archetype *arch, u64 *outEntity)
{
    if(arch == NULL || outEntity == NULL)
    {
        ERROR("createEntityInArchetype: Invalid parameters");
        return false;
    }

    //for now we only support one arena per archetype
    if(arch->arenaCount == 0)
    {
        ERROR("createEntityInArchetype: No arenas in archetype");
        return false;
    }

    // reject if isSingle and we already have an entity
    if(arch->isSingle && arch->arena[0].count >= 1)
    {
        ERROR("createEntityInArchetype: isSingle archetype already has an entity");
        return false;
    }

    u32 index = createEntity(&arch->arena[0]);
    if(index == 0)
    {
        ERROR("createEntityInArchetype: Failed to create entity in arena");
        return false;
    }

    //pack the archetype id and index into a u64
    *outEntity = ((u64)arch->id << 32) | (u64)(index - 1); // index is 1-based

    return true;
}

//remove an entity from the given archetype
b8 removeEntityFromArchetype(Archetype *arch,u32 arenaIndex, u32 index)
{
    if(arch == NULL)
    {
        ERROR("removeEntityFromArchetype: Invalid archetype");
        return false;
    }

    //for now we only support one arena per archetype
    if(arch->arenaCount == 0)
    {
        ERROR("removeEntityFromArchetype: No arenas in archetype");
        return false;
    }

    return removeEntityFromArena(&arch->arena[arenaIndex], index);
}


//get the archetype soa fields pointers
void **getArchetypeFields(Archetype *arch, u32 arenaIndex)
{
    if(arch == NULL || arch->arenaCount == 0)
    {
        ERROR("getArchetypeFields: Invalid archetype or no arenas");
        return NULL;
    }

    if(arenaIndex >= arch->arenaCount)
    {
        ERROR("getArchetypeFields: Invalid arena index");
        return NULL;
    }



    return arch->arena[arenaIndex].fields;
}

