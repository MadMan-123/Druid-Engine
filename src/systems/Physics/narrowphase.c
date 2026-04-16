
#include "../../../include/druid.h"
#include <math.h>

//=====================================================================================================================
// Forward declarations for internal physics bindings

// Must match the struct in physics.c exactly
typedef struct
{
    i32 posX, posY, posZ;
    i32 velX, velY, velZ;
    i32 forceX, forceY, forceZ;
    i32 bodyType;
    i32 mass, invMass;
    i32 restitution;
    i32 damping;
    i32 radius;
    i32 halfX, halfY, halfZ;
    i32 shape;
    i32 scaleField;
    u32 scaleFieldSize;
} PhysFieldBinding;

extern PhysFieldBinding *physGetBindingByIndex(PhysicsWorld *world, u32 index);

//=====================================================================================================================
// Raycasting

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

        PhysFieldBinding *b = physGetBindingByIndex(world, a);
        if (!b || b->posX < 0 || b->posY < 0 || b->posZ < 0) continue;

        for (u32 c = 0; c < arch->activeChunkCount; c++)
        {
            void **fields = getArchetypeFields(arch, c);
            if (!fields) continue;
            u32 count = arch->arena[c].count;

            f32 *posX = (f32 *)fields[b->posX];
            f32 *posY = (f32 *)fields[b->posY];
            f32 *posZ = (f32 *)fields[b->posZ];
            f32 *rad  = (b->radius >= 0) ? (f32 *)fields[b->radius] : NULL;
            f32 *hx   = (b->halfX >= 0)  ? (f32 *)fields[b->halfX]  : NULL;
            f32 *hy   = (b->halfY >= 0)  ? (f32 *)fields[b->halfY]  : NULL;
            f32 *hz   = (b->halfZ >= 0)  ? (f32 *)fields[b->halfZ]  : NULL;

            for (u32 i = 0; i < count; i++)
            {
                Vec3 center = {posX[i], posY[i], posZ[i]};
                f32 halfExtX = hx ? hx[i] : 0.0f;
                f32 halfExtY = hy ? hy[i] : 0.0f;
                f32 halfExtZ = hz ? hz[i] : 0.0f;
                b8 isBox = (halfExtX > 0.0f || halfExtY > 0.0f || halfExtZ > 0.0f);

                if (isBox)
                {
                    // ray-AABB slab intersection
                    f32 tmin = 0.0f;
                    f32 tmax = hit.distance;
                    Vec3 n = {0, 1, 0};

                    f32 bmin, bmax, invD, t1, t2, nAxis;

                    // X slab
                    bmin = center.x - halfExtX;
                    bmax = center.x + halfExtX;
                    if (fabsf(ray.direction.x) < 1e-8f)
                    {
                        if (ray.origin.x < bmin || ray.origin.x > bmax) continue;
                    }
                    else
                    {
                        invD = 1.0f / ray.direction.x;
                        t1 = (bmin - ray.origin.x) * invD;
                        t2 = (bmax - ray.origin.x) * invD;
                        nAxis = -1.0f;
                        if (t1 > t2) { f32 tmp = t1; t1 = t2; t2 = tmp; nAxis = 1.0f; }
                        if (t1 > tmin) { tmin = t1; n = (Vec3){nAxis, 0, 0}; }
                        if (t2 < tmax) tmax = t2;
                        if (tmin > tmax) continue;
                    }

                    // Y slab
                    bmin = center.y - halfExtY;
                    bmax = center.y + halfExtY;
                    if (fabsf(ray.direction.y) < 1e-8f)
                    {
                        if (ray.origin.y < bmin || ray.origin.y > bmax) continue;
                    }
                    else
                    {
                        invD = 1.0f / ray.direction.y;
                        t1 = (bmin - ray.origin.y) * invD;
                        t2 = (bmax - ray.origin.y) * invD;
                        nAxis = -1.0f;
                        if (t1 > t2) { f32 tmp = t1; t1 = t2; t2 = tmp; nAxis = 1.0f; }
                        if (t1 > tmin) { tmin = t1; n = (Vec3){0, nAxis, 0}; }
                        if (t2 < tmax) tmax = t2;
                        if (tmin > tmax) continue;
                    }

                    // Z slab
                    bmin = center.z - halfExtZ;
                    bmax = center.z + halfExtZ;
                    if (fabsf(ray.direction.z) < 1e-8f)
                    {
                        if (ray.origin.z < bmin || ray.origin.z > bmax) continue;
                    }
                    else
                    {
                        invD = 1.0f / ray.direction.z;
                        t1 = (bmin - ray.origin.z) * invD;
                        t2 = (bmax - ray.origin.z) * invD;
                        nAxis = -1.0f;
                        if (t1 > t2) { f32 tmp = t1; t1 = t2; t2 = tmp; nAxis = 1.0f; }
                        if (t1 > tmin) { tmin = t1; n = (Vec3){0, 0, nAxis}; }
                        if (t2 < tmax) tmax = t2;
                        if (tmin > tmax) continue;
                    }

                    if (tmin <= 0.0f) continue;
                    if (tmin > hit.distance) continue;

                    hit.hit = true;
                    hit.distance = tmin;
                    hit.point = v3Add(ray.origin, v3Scale(ray.direction, tmin));
                    hit.normal = n;
                    hit.bodyIndex = c * arch->chunkCapacity + i;
                }
                else
                {
                    // ray-sphere intersection
                    f32 r = rad ? rad[i] : 0.5f;
                    Vec3 oc = v3Sub(ray.origin, center);
                    f32 bd = v3Dot(oc, ray.direction);
                    f32 cc = v3Dot(oc, oc) - r * r;
                    f32 disc = bd * bd - cc;
                    if (disc < 0.0f) continue;

                    f32 sqrtDisc = sqrtf(disc);
                    f32 t = -bd - sqrtDisc;
                    if (t <= 0.0f) continue;
                    if (t > hit.distance) continue;

                    hit.hit = true;
                    hit.distance = t;
                    hit.point = v3Add(ray.origin, v3Scale(ray.direction, t));
                    hit.normal = v3Norm(v3Sub(hit.point, center));
                    hit.bodyIndex = c * arch->chunkCapacity + i;
                }
            }
        }
    }
    return hit;
}

u32 physRaycastAll(PhysicsWorld *world, PhysRay ray, PhysRayHit *hits, u32 maxHits)
{
    u32 hitCount = 0;
    if (!world || !hits || maxHits == 0) return 0;

    u32 archCount = physGetBodyArchetypeCount(world);
    for (u32 a = 0; a < archCount; a++)
    {
        Archetype *arch = physGetBodyArchetype(world, a);
        if (!arch) continue;

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

            for (u32 i = 0; i < count && hitCount < maxHits; i++)
            {
                f32 r = rad ? rad[i] : 0.5f;
                Vec3 center = {px[i], py[i], pz[i]};

                Vec3 oc = v3Sub(ray.origin, center);
                f32 bd = v3Dot(oc, ray.direction);
                f32 cc = v3Dot(oc, oc) - r * r;
                f32 disc = bd * bd - cc;
                if (disc < 0.0f) continue;

                f32 sqrtDisc = sqrtf(disc);
                f32 t = -bd - sqrtDisc;
                if (t < 0.0f) t = -bd + sqrtDisc;
                if (t < 0.0f || t > ray.maxDistance) continue;

                PhysRayHit *h = &hits[hitCount++];
                h->hit = true;
                h->distance = t;
                h->point = v3Add(ray.origin, v3Scale(ray.direction, t));
                h->normal = v3Norm(v3Sub(h->point, center));
                h->bodyIndex = c * arch->chunkCapacity + i;
            }
        }
    }
    return hitCount;
}
