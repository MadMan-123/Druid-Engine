
#include "../../../include/druid.h"
#include <math.h>

//=====================================================================================================================
// Forward declarations for internal physics bindings
//=====================================================================================================================

// Defined in physics.c — represents cached field indices for a registered archetype
typedef struct
{
    i32 posX, posY, posZ;
    i32 velX, velY, velZ;
    i32 radius;
    i32 halfX, halfY, halfZ;
    i32 bodyType;
    i32 mass;
} PhysFieldBinding;

// Accessor function for cached field bindings
extern PhysFieldBinding *physGetBindingByIndex(PhysicsWorld *world, u32 index);

//=====================================================================================================================
// Spatial Hash Grid — placeholder for future spatial partitioning
//=====================================================================================================================

struct SpatialHashGrid
{
    f32  cellSize;
    u32  tableSize;
    u32 *cellStart;
    u32 *cellCount;
    u32 *entries;
    u32  entryCapacity;
    u32  entryCount;
};

//=====================================================================================================================
// Broadphase API
//=====================================================================================================================

void physBroadphaseRebuild(PhysicsWorld *world)
{
    // Pair generation is done inline inside physWorldStep (brute-force N*N).
    // This function is reserved for explicit spatial hash rebuilds in the future.
    (void)world;
}

u32 physBroadphaseQuery(PhysicsWorld *world, AABB query, u32 *outBodies, u32 maxBodies)
{
    if (!world || !outBodies || maxBodies == 0) return 0;

    u32 found = 0;
    u32 archCount = physGetBodyArchetypeCount(world);

    for (u32 a = 0; a < archCount && found < maxBodies; a++)
    {
        Archetype *arch = physGetBodyArchetype(world, a);
        if (!arch) continue;

        // OPTIMIZED: O(1) cached binding lookup, no strcmp
        PhysFieldBinding *b = physGetBindingByIndex(world, a);
        if (!b || b->posX < 0 || b->posY < 0 || b->posZ < 0) continue;

        for (u32 c = 0; c < arch->activeChunkCount && found < maxBodies; c++)
        {
            void **fields = getArchetypeFields(arch, c);
            if (!fields) continue;
            u32 count = arch->arena[c].count;

            f32 *px = (f32 *)fields[b->posX];
            f32 *py = (f32 *)fields[b->posY];
            f32 *pz = (f32 *)fields[b->posZ];
            f32 *rad = (b->radius >= 0) ? (f32 *)fields[b->radius] : NULL;

            for (u32 i = 0; i < count && found < maxBodies; i++)
            {
                f32 r = rad ? rad[i] : 0.0f;
                AABB body = {
                    {px[i] - r, py[i] - r, pz[i] - r},
                    {px[i] + r, py[i] + r, pz[i] + r}
                };
                if (isAABBOverlapping(body, query))
                {
                    outBodies[found++] = c * arch->chunkCapacity + i;
                }
            }
        }
    }
    return found;
}
