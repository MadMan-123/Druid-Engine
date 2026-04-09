
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
// Narrowphase queries — raycasts and overlap tests against registered archetypes
//
// All bodies are treated as spheres (position + SphereRadius field).
// GJK/EPA and mesh BVH are future work.
//=====================================================================================================================

//=====================================================================================================================
// Raycasting — ray vs sphere for all registered bodies
//=====================================================================================================================

PhysRayHit physRaycast(PhysicsWorld *world, PhysRay ray)
{
    PhysRayHit hit = {0};
    hit.hit = false;
    hit.distance = ray.maxDistance;
    if (!world) return hit;

    u32 archCount = physGetBodyArchetypeCount(world);
    for (u32 a = 0; a < archCount; a++)
    {
        Archetype *arch = physGetBodyArchetype(world, a);
        if (!arch) continue;

        // OPTIMIZED: O(1) cached binding lookup instead of findPosRadFields with strcmp
        PhysFieldBinding *b = physGetBindingByIndex(world, a);
        if (!b || b->posX < 0 || b->posY < 0 || b->posZ < 0) continue;

        for (u32 c = 0; c < arch->activeChunkCount; c++)
        {
            void **fields = getArchetypeFields(arch, c);
            if (!fields) continue;
            u32 count = arch->arena[c].count;

            f32 *px = (f32 *)fields[b->posX];
            f32 *py = (f32 *)fields[b->posY];
            f32 *pz = (f32 *)fields[b->posZ];
            f32 *rad = (b->radius >= 0) ? (f32 *)fields[b->radius] : NULL;

            for (u32 i = 0; i < count; i++)
            {
                f32 r = rad ? rad[i] : 0.5f;
                Vec3 center = {px[i], py[i], pz[i]};

                // ray-sphere intersection
                Vec3 oc = v3Sub(ray.origin, center);
                f32 b = v3Dot(oc, ray.direction);
                f32 cc = v3Dot(oc, oc) - r * r;
                f32 disc = b * b - cc;
                if (disc < 0.0f) continue;

                f32 sqrtDisc = sqrtf(disc);
                f32 t = -b - sqrtDisc;
                if (t < 0.0f) t = -b + sqrtDisc;
                if (t < 0.0f || t > hit.distance) continue;

                hit.hit = true;
                hit.distance = t;
                hit.point = v3Add(ray.origin, v3Scale(ray.direction, t));
                hit.normal = v3Norm(v3Sub(hit.point, center));
                hit.bodyIndex = c * arch->chunkCapacity + i;
            }
        }
    }
    return hit;
}

u32 physRaycastAll(PhysicsWorld *world, PhysRay ray, PhysRayHit *hits, u32 maxHits)
{
    if (!world || !hits || maxHits == 0) return 0;

    u32 found = 0;
    u32 archCount = physGetBodyArchetypeCount(world);

    for (u32 a = 0; a < archCount && found < maxHits; a++)
    {
        Archetype *arch = physGetBodyArchetype(world, a);
        if (!arch) continue;

        // OPTIMIZED: O(1) cached binding lookup instead of findPosRadFields with strcmp
        PhysFieldBinding *b = physGetBindingByIndex(world, a);
        if (!b || b->posX < 0 || b->posY < 0 || b->posZ < 0) continue;

        for (u32 c = 0; c < arch->activeChunkCount && found < maxHits; c++)
        {
            void **fields = getArchetypeFields(arch, c);
            if (!fields) continue;
            u32 count = arch->arena[c].count;

            f32 *px = (f32 *)fields[b->posX];
            f32 *py = (f32 *)fields[b->posY];
            f32 *pz = (f32 *)fields[b->posZ];
            f32 *rad = (b->radius >= 0) ? (f32 *)fields[b->radius] : NULL;

            for (u32 i = 0; i < count && found < maxHits; i++)
            {
                f32 r = rad ? rad[i] : 0.5f;
                Vec3 center = {px[i], py[i], pz[i]};

                Vec3 oc = v3Sub(ray.origin, center);
                f32 b = v3Dot(oc, ray.direction);
                f32 cc = v3Dot(oc, oc) - r * r;
                f32 disc = b * b - cc;
                if (disc < 0.0f) continue;

                f32 sqrtDisc = sqrtf(disc);
                f32 t = -b - sqrtDisc;
                if (t < 0.0f) t = -b + sqrtDisc;
                if (t < 0.0f || t > ray.maxDistance) continue;

                PhysRayHit *h = &hits[found++];
                h->hit = true;
                h->distance = t;
                h->point = v3Add(ray.origin, v3Scale(ray.direction, t));
                h->normal = v3Norm(v3Sub(h->point, center));
                h->bodyIndex = c * arch->chunkCapacity + i;
            }
        }
    }
    return found;
}

//=====================================================================================================================
// Overlap queries
//=====================================================================================================================

u32 physOverlapSphere(PhysicsWorld *world, Vec3 center, f32 radius,
                      u32 *outBodies, u32 maxBodies)
{
    if (!world || !outBodies || maxBodies == 0) return 0;

    u32 found = 0;
    u32 archCount = physGetBodyArchetypeCount(world);

    for (u32 a = 0; a < archCount && found < maxBodies; a++)
    {
        Archetype *arch = physGetBodyArchetype(world, a);
        if (!arch) continue;

        // OPTIMIZED: O(1) cached binding lookup instead of findPosRadFields with strcmp
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
                f32 dx = px[i] - center.x;
                f32 dy = py[i] - center.y;
                f32 dz = pz[i] - center.z;
                f32 sumR = radius + r;
                if (dx*dx + dy*dy + dz*dz <= sumR * sumR)
                {
                    outBodies[found++] = c * arch->chunkCapacity + i;
                }
            }
        }
    }
    return found;
}

u32 physOverlapBox(PhysicsWorld *world, Vec3 center, Vec3 halfExtents,
                   u32 *outBodies, u32 maxBodies)
{
    if (!world || !outBodies || maxBodies == 0) return 0;

    AABB query = {v3Sub(center, halfExtents), v3Add(center, halfExtents)};
    return physBroadphaseQuery(world, query, outBodies, maxBodies);
}
