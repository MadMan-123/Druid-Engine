
#include "../../../include/druid.h"
#include <math.h>
#include <string.h>

//=====================================================================================================================
// Constants
//=====================================================================================================================

#define MAX_PHYS_ARCHETYPES  16
#define MAX_PHYS_PAIRS       65536
#define MAX_PHYS_MANIFOLDS   32768
#define MAX_PHYS_BODIES      1048576

// Collider shape tags (match scene entity: 1=Sphere, 2=Box)
#define PSHAPE_SPHERE 1u
#define PSHAPE_BOX    2u

//=====================================================================================================================
// PhysFieldBinding — cached per-archetype field indices, built once at registration
//=====================================================================================================================

typedef struct
{
    i32 posX, posY, posZ;
    i32 velX, velY, velZ;
    i32 forceX, forceY, forceZ;
    i32 bodyType;
    i32 mass, invMass;
    i32 restitution;
    i32 damping;
    // Collider shape fields
    i32 radius;               // SphereRadius
    i32 halfX, halfY, halfZ;  // ColliderHalfX/Y/Z
    i32 shape;                // ColliderShape field (optional, used if present)
    // Scale — for fallback when radius/half are 0
    i32 scaleField;
    u32 scaleFieldSize;       // sizeof(f32) or sizeof(Vec3)
} PhysFieldBinding;

//=====================================================================================================================
// CollisionPair — indices into registered archetypes
//=====================================================================================================================

typedef struct
{
    u32 archA,  indexA, chunkA;
    u32 archB,  indexB, chunkB;
    u32 shapeA, shapeB;  // cached from broadphase — avoids re-inferring in narrowphase
} CollisionPair;

//=====================================================================================================================
// Flat body SoA arrays — rebuilt every substep for the spatial hash
//
// DoD: separate arrays per field so broadphase iteration (position + radius only)
// touches minimal cache lines. AoS PhysBodyFlat was 48 bytes/body — broadphase
// only needs pos+radius (16 bytes) but loaded 48 bytes per body per cache line.
//=====================================================================================================================

static f32 g_bpPosX[MAX_PHYS_BODIES];
static f32 g_bpPosY[MAX_PHYS_BODIES];
static f32 g_bpPosZ[MAX_PHYS_BODIES];
static f32 g_bpRadius[MAX_PHYS_BODIES];    // bounding sphere radius
static f32 g_bpHalfX[MAX_PHYS_BODIES];     // box half-extents (0 if sphere)
static f32 g_bpHalfY[MAX_PHYS_BODIES];
static f32 g_bpHalfZ[MAX_PHYS_BODIES];
static u32 g_bpShape[MAX_PHYS_BODIES];     // PSHAPE_SPHERE or PSHAPE_BOX
static u32 g_bpBodyType[MAX_PHYS_BODIES];
static u32 g_bpArchIdx[MAX_PHYS_BODIES];
static u32 g_bpChunkIdx[MAX_PHYS_BODIES];
static u32 g_bpEntityIdx[MAX_PHYS_BODIES];
static u32 g_bodyCount;

//=====================================================================================================================
// Spatial hash grid — single-cell insertion + 27-neighbor query
//=====================================================================================================================

#define SH_TABLE_SIZE 262144u  // power of 2, sized for up to 1M bodies
#define SH_SENTINEL   0xFFFFFFFFu

static u32 g_shTable[SH_TABLE_SIZE];
static u32 g_shNext[MAX_PHYS_BODIES];
static f32 g_shCellSize;
static f32 g_shInvCell;

// Dirty bucket tracking — only clear buckets that were actually used
static u32 g_shDirtyBuckets[SH_TABLE_SIZE];
static u32 g_shDirtyCount;

static inline i32 sh_floor(f32 v)
{
    return (i32)floorf(v * g_shInvCell);
}

static inline u32 sh_hash(i32 ix, i32 iy, i32 iz)
{
    u32 h = (u32)((ix * 92837111) ^ (iy * 689287499) ^ (iz * 283923481));
    return h & (SH_TABLE_SIZE - 1u);
}

static void sh_clear(void)
{
    // Only clear buckets that were actually touched — avoids memset of 65K entries
    for (u32 i = 0; i < g_shDirtyCount; i++) g_shTable[g_shDirtyBuckets[i]] = SH_SENTINEL;
    for (u32 i = 0; i < g_bodyCount;    i++) g_shNext[i] = SH_SENTINEL;
    g_shDirtyCount = 0;
}

static void sh_insert(u32 idx)
{
    u32 h = sh_hash(sh_floor(g_bpPosX[idx]), sh_floor(g_bpPosY[idx]), sh_floor(g_bpPosZ[idx]));
    if (g_shTable[h] == SH_SENTINEL && g_shDirtyCount < SH_TABLE_SIZE)
        g_shDirtyBuckets[g_shDirtyCount++] = h;
    g_shNext[idx] = g_shTable[h];
    g_shTable[h]  = idx;
}

//=====================================================================================================================
// PhysicsWorld struct
//=====================================================================================================================

struct PhysicsWorld
{
    Vec3 gravity;
    f32  fixedTimestep;
    u32  solverIterations;
    u32  substeps;
    f32  accumulator;

    Archetype       *bodyArchetypes[MAX_PHYS_ARCHETYPES];
    PhysFieldBinding bindings[MAX_PHYS_ARCHETYPES];
    u32              bodyArchetypeCount;

    CollisionPair    pairs[MAX_PHYS_PAIRS];
    u32              pairCount;

    ContactManifold  manifolds[MAX_PHYS_MANIFOLDS];
    u32              manifoldCount;

    CollisionFn onCollision;
    TriggerFn   onTrigger;

    SpatialHashGrid *grid; // unused — hash is managed as file-scope statics

    // Visibility-based compute LOD — set each frame via physWorldSetVisibility()
    const b8   *visibility;       // external buffer (frame-allocated, not owned)
    u32         visCount;         // entries in visibility[]
    u32         lodInterval;      // non-visible entities update every N steps (0 = skip entirely)
    u32         physFrameCounter; // internal counter for LOD scheduling
};

//=====================================================================================================================
// Field binding helpers
//=====================================================================================================================

static i32 findField(StructLayout *layout, const c8 *name)
{
    for (u32 i = 0; i < layout->count; i++)
        if (strcmp(layout->fields[i].name, name) == 0) return (i32)i;
    return -1;
}

static PhysFieldBinding buildBinding(StructLayout *layout)
{
    PhysFieldBinding b;
    b.posX  = findField(layout, "PositionX");
    b.posY  = findField(layout, "PositionY");
    b.posZ  = findField(layout, "PositionZ");
    if (b.posX < 0) b.posX = findField(layout, "positionX");
    if (b.posY < 0) b.posY = findField(layout, "positionY");
    if (b.posZ < 0) b.posZ = findField(layout, "positionZ");

    b.velX    = findField(layout, "LinearVelocityX");
    b.velY    = findField(layout, "LinearVelocityY");
    b.velZ    = findField(layout, "LinearVelocityZ");
    b.forceX  = findField(layout, "ForceX");
    b.forceY  = findField(layout, "ForceY");
    b.forceZ  = findField(layout, "ForceZ");
    b.bodyType   = findField(layout, "PhysicsBodyType");
    if (b.bodyType < 0) b.bodyType = findField(layout, "PhysBodyType");
    b.mass       = findField(layout, "Mass");
    b.invMass    = findField(layout, "InvMass");
    b.restitution = findField(layout, "Restitution");
    b.damping    = findField(layout, "LinearDamping");
    b.radius     = findField(layout, "SphereRadius");
    if (b.radius < 0) b.radius = findField(layout, "Radius");
    b.halfX      = findField(layout, "ColliderHalfX");
    b.halfY      = findField(layout, "ColliderHalfY");
    b.halfZ      = findField(layout, "ColliderHalfZ");
    b.shape      = findField(layout, "ColliderShape");

    // Scale — prefer Vec3, fall back to f32 uniform
    b.scaleField = findField(layout, "Scale");
    if (b.scaleField < 0) b.scaleField = findField(layout, "scale");
    b.scaleFieldSize = (b.scaleField >= 0) ? layout->fields[b.scaleField].size : 0;
    return b;
}

//=====================================================================================================================
// Effective collider size helpers — scale fallback when explicit sizes are 0
//=====================================================================================================================

static f32 clampf_local(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

static f32 effectiveRadius(void **fields, PhysFieldBinding *b, u32 i)
{
    f32 r = (b->radius >= 0) ? ((f32 *)fields[b->radius])[i] : 0.0f;
    if (r > 0.0f) return r;
    // Fallback: derive from Scale
    if (b->scaleField >= 0)
    {
        if (b->scaleFieldSize >= sizeof(Vec3))
        {
            Vec3 s = ((Vec3 *)fields[b->scaleField])[i];
            f32 m = s.x > s.y ? s.x : s.y;
            if (s.z > m) m = s.z;
            return m * 0.5f;
        }
        else if (b->scaleFieldSize == sizeof(f32))
        {
            return ((f32 *)fields[b->scaleField])[i] * 0.5f;
        }
    }
    return 0.5f;
}

static Vec3 effectiveHalf(void **fields, PhysFieldBinding *b, u32 i)
{
    f32 hx = (b->halfX >= 0) ? ((f32 *)fields[b->halfX])[i] : 0.0f;
    f32 hy = (b->halfY >= 0) ? ((f32 *)fields[b->halfY])[i] : 0.0f;
    f32 hz = (b->halfZ >= 0) ? ((f32 *)fields[b->halfZ])[i] : 0.0f;
    if (hx > 0.0f || hy > 0.0f || hz > 0.0f)
        return (Vec3){hx, hy, hz};
    // Fallback: half of Scale
    if (b->scaleField >= 0)
    {
        if (b->scaleFieldSize >= sizeof(Vec3))
        {
            Vec3 s = ((Vec3 *)fields[b->scaleField])[i];
            return (Vec3){s.x * 0.5f, s.y * 0.5f, s.z * 0.5f};
        }
        else if (b->scaleFieldSize == sizeof(f32))
        {
            f32 h = ((f32 *)fields[b->scaleField])[i] * 0.5f;
            return (Vec3){h, h, h};
        }
    }
    return (Vec3){0.5f, 0.5f, 0.5f};
}

static u32 inferShape(void **fields, PhysFieldBinding *b, u32 i)
{
    // Explicit shape field wins
    if (b->shape >= 0)
    {
        u32 s = ((u32 *)fields[b->shape])[i];
        if (s == PSHAPE_SPHERE || s == PSHAPE_BOX) return s;
    }
    // Infer from data: box if any half-extent is set, else sphere
    f32 hx = (b->halfX >= 0) ? ((f32 *)fields[b->halfX])[i] : 0.0f;
    f32 hy = (b->halfY >= 0) ? ((f32 *)fields[b->halfY])[i] : 0.0f;
    f32 hz = (b->halfZ >= 0) ? ((f32 *)fields[b->halfZ])[i] : 0.0f;
    f32 r  = (b->radius >= 0) ? ((f32 *)fields[b->radius])[i] : 0.0f;
    if ((hx > 0.0f || hy > 0.0f || hz > 0.0f) && r == 0.0f)
        return PSHAPE_BOX;
    return PSHAPE_SPHERE;
}

//=====================================================================================================================
// Flat body list build — iterate all registered archetypes each substep
//=====================================================================================================================

static void buildBodyList(PhysicsWorld *world)
{
    g_bodyCount = 0;
    const b8 *vis = world->visibility;
    u32 visCount  = world->visCount;
    u32 visIdx    = 0;

    for (u32 a = 0; a < world->bodyArchetypeCount; a++)
    {
        Archetype *arch = world->bodyArchetypes[a];
        PhysFieldBinding *b = &world->bindings[a];
        if (b->posX < 0) continue;

        for (u32 c = 0; c < arch->activeChunkCount; c++)
        {
            void **fields = getArchetypeFields(arch, c);
            if (!fields) continue;
            u32 count = arch->arena[c].count;
            f32 *posX = (f32 *)fields[b->posX];
            f32 *posY = (f32 *)fields[b->posY];
            f32 *posZ = (f32 *)fields[b->posZ];
            u32 *bt   = (b->bodyType >= 0) ? (u32 *)fields[b->bodyType] : NULL;

            for (u32 i = 0; i < count && g_bodyCount < MAX_PHYS_BODIES; i++)
            {
                // Skip non-visible entities from collision detection entirely
                if (vis && (visIdx + i) < visCount && !vis[visIdx + i])
                    continue;
                u32 idx = g_bodyCount++;
                g_bpArchIdx[idx]   = a;
                g_bpChunkIdx[idx]  = c;
                g_bpEntityIdx[idx] = i;
                g_bpPosX[idx]      = posX[i];
                g_bpPosY[idx]      = posY[i];
                g_bpPosZ[idx]      = posZ[i];
                g_bpBodyType[idx]  = bt ? bt[i] : PHYS_BODY_DYNAMIC;
                g_bpShape[idx]     = inferShape(fields, b, i);

                if (g_bpShape[idx] == PSHAPE_BOX)
                {
                    Vec3 h = effectiveHalf(fields, b, i);
                    g_bpHalfX[idx] = h.x;
                    g_bpHalfY[idx] = h.y;
                    g_bpHalfZ[idx] = h.z;
                    g_bpRadius[idx] = sqrtf(h.x*h.x + h.y*h.y + h.z*h.z);
                }
                else
                {
                    g_bpRadius[idx] = effectiveRadius(fields, b, i);
                    g_bpHalfX[idx] = g_bpHalfY[idx] = g_bpHalfZ[idx] = 0.0f;
                }
            }
            visIdx += count;
        }
    }
}

//=====================================================================================================================
// Spatial hash broadphase — O(N) build, O(N·27·k) query
//=====================================================================================================================

static u32 broadphaseGetPairs(CollisionPair *pairs, u32 maxPairs)
{
    if (g_bodyCount == 0) return 0;

    // Cell size = 2 * max bounding radius of DYNAMIC bodies only.
    // Using max across ALL bodies (including large static bodies like a sun) causes
    // a few large bodies to inflate the cell size, collapsing the whole scene into
    // 3-7 cells per axis → chain lengths of 100s → near-O(n²) broadphase.
    // Static bodies are handled correctly regardless — they just get inserted at their
    // cell and will be found by any dynamic body querying the 27 neighbors.
    f32 maxR = 0.0f;
    for (u32 i = 0; i < g_bodyCount; i++)
        if (g_bpBodyType[i] != PHYS_BODY_STATIC && g_bpRadius[i] > maxR)
            maxR = g_bpRadius[i];
    // Fall back to overall max if no dynamic bodies have a meaningful radius
    if (maxR < 0.01f)
        for (u32 i = 0; i < g_bodyCount; i++)
            if (g_bpRadius[i] > maxR) maxR = g_bpRadius[i];
    g_shCellSize = maxR * 2.0f;
    if (g_shCellSize < 0.01f) g_shCellSize = 1.0f;
    g_shInvCell  = 1.0f / g_shCellSize;

    sh_clear();
    for (u32 i = 0; i < g_bodyCount; i++) sh_insert(i);

    u32 pairCount = 0;
    for (u32 i = 0; i < g_bodyCount; i++)
    {
        i32 cx = sh_floor(g_bpPosX[i]);
        i32 cy = sh_floor(g_bpPosY[i]);
        i32 cz = sh_floor(g_bpPosZ[i]);

        for (i32 nx = cx - 1; nx <= cx + 1; nx++)
        for (i32 ny = cy - 1; ny <= cy + 1; ny++)
        for (i32 nz = cz - 1; nz <= cz + 1; nz++)
        {
            u32 h = sh_hash(nx, ny, nz);
            u32 j = g_shTable[h];
            while (j != SH_SENTINEL)
            {
                if (j > i) // generate each pair exactly once
                {
                    // Both static — no collision response needed
                    if (g_bpBodyType[i] == PHYS_BODY_STATIC &&
                        g_bpBodyType[j] == PHYS_BODY_STATIC)
                    {
                        j = g_shNext[j]; continue;
                    }
                    // Bounding sphere overlap test (fast reject)
                    f32 dx = g_bpPosX[i] - g_bpPosX[j];
                    f32 dy = g_bpPosY[i] - g_bpPosY[j];
                    f32 dz = g_bpPosZ[i] - g_bpPosZ[j];
                    f32 sr = g_bpRadius[i] + g_bpRadius[j];
                    if (dx*dx + dy*dy + dz*dz < sr * sr)
                    {
                        if (pairCount < maxPairs)
                        {
                            CollisionPair *p = &pairs[pairCount++];
                            p->archA  = g_bpArchIdx[i];  p->chunkA = g_bpChunkIdx[i]; p->indexA = g_bpEntityIdx[i];
                            p->archB  = g_bpArchIdx[j];  p->chunkB = g_bpChunkIdx[j]; p->indexB = g_bpEntityIdx[j];
                            p->shapeA = g_bpShape[i];
                            p->shapeB = g_bpShape[j];
                        }
                    }
                }
                j = g_shNext[j];
            }
        }
    }
    return pairCount;
}

//=====================================================================================================================
// Narrowphase contact generation — sphere-sphere, sphere-box, box-box
//=====================================================================================================================

static b8 contactSphereSphere(Vec3 pa, f32 ra, Vec3 pb, f32 rb, ContactManifold *m)
{
    f32 dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
    f32 d2 = dx*dx + dy*dy + dz*dz;
    f32 sumR = ra + rb;
    if (d2 >= sumR * sumR || d2 < 1e-12f) return false;

    f32 dist = sqrtf(d2), inv = 1.0f / dist;
    m->pointCount = 1;
    m->points[0].normal = (Vec3){dx*inv, dy*inv, dz*inv};
    m->points[0].depth  = sumR - dist;
    m->points[0].point  = (Vec3){pa.x + m->points[0].normal.x * ra,
                                  pa.y + m->points[0].normal.y * ra,
                                  pa.z + m->points[0].normal.z * ra};
    return true;
}

// normal points FROM box TOWARD sphere (i.e. push-out direction for sphere)
static b8 contactSphereBox(Vec3 sp, f32 sr, Vec3 bp, Vec3 bh, ContactManifold *m)
{
    f32 cpx = clampf_local(sp.x, bp.x - bh.x, bp.x + bh.x);
    f32 cpy = clampf_local(sp.y, bp.y - bh.y, bp.y + bh.y);
    f32 cpz = clampf_local(sp.z, bp.z - bh.z, bp.z + bh.z);

    f32 dx = sp.x - cpx, dy = sp.y - cpy, dz = sp.z - cpz;
    f32 d2 = dx*dx + dy*dy + dz*dz;
    if (d2 >= sr * sr && d2 > 1e-12f) return false;

    m->pointCount = 1;
    ContactPoint *cp = &m->points[0];
    cp->point = (Vec3){cpx, cpy, cpz};

    if (d2 > 1e-12f)
    {
        f32 dist = sqrtf(d2);
        cp->normal = (Vec3){dx / dist, dy / dist, dz / dist};
        cp->depth  = sr - dist;
    }
    else
    {
        // Sphere center inside box — minimum penetration axis
        f32 ox = bh.x - fabsf(sp.x - bp.x);
        f32 oy = bh.y - fabsf(sp.y - bp.y);
        f32 oz = bh.z - fabsf(sp.z - bp.z);
        if (ox <= oy && ox <= oz)
        {
            cp->normal = (Vec3){sp.x > bp.x ? 1.0f : -1.0f, 0.0f, 0.0f};
            cp->depth  = ox + sr;
        }
        else if (oy <= oz)
        {
            cp->normal = (Vec3){0.0f, sp.y > bp.y ? 1.0f : -1.0f, 0.0f};
            cp->depth  = oy + sr;
        }
        else
        {
            cp->normal = (Vec3){0.0f, 0.0f, sp.z > bp.z ? 1.0f : -1.0f};
            cp->depth  = oz + sr;
        }
    }
    return true;
}

// normal points FROM B TOWARD A
static b8 contactBoxBox(Vec3 pa, Vec3 ha, Vec3 pb, Vec3 hb, ContactManifold *m)
{
    f32 ox = ha.x + hb.x - fabsf(pa.x - pb.x);
    f32 oy = ha.y + hb.y - fabsf(pa.y - pb.y);
    f32 oz = ha.z + hb.z - fabsf(pa.z - pb.z);
    if (ox <= 0.0f || oy <= 0.0f || oz <= 0.0f) return false;

    m->pointCount = 1;
    ContactPoint *cp = &m->points[0];
    cp->point = (Vec3){(pa.x + pb.x) * 0.5f,
                        (pa.y + pb.y) * 0.5f,
                        (pa.z + pb.z) * 0.5f};

    if (ox <= oy && ox <= oz)
    {
        cp->normal = (Vec3){pa.x > pb.x ? 1.0f : -1.0f, 0.0f, 0.0f};
        cp->depth  = ox;
    }
    else if (oy <= oz)
    {
        cp->normal = (Vec3){0.0f, pa.y > pb.y ? 1.0f : -1.0f, 0.0f};
        cp->depth  = oy;
    }
    else
    {
        cp->normal = (Vec3){0.0f, 0.0f, pa.z > pb.z ? 1.0f : -1.0f};
        cp->depth  = oz;
    }
    return true;
}

//=====================================================================================================================
// World lifecycle
//=====================================================================================================================

PhysicsWorld *physWorldCreate(Vec3 gravity, f32 timestep)
{
    PhysicsWorld *world = (PhysicsWorld *)dalloc(sizeof(PhysicsWorld), MEM_TAG_PHYSICS);
    if (!world) return NULL;
    memset(world, 0, sizeof(PhysicsWorld));
    world->gravity       = gravity;
    world->fixedTimestep = timestep;
    world->solverIterations = 8;
    world->substeps      = 1;

    // Initialize spatial hash table — dirty tracking only clears used buckets
    for (u32 i = 0; i < SH_TABLE_SIZE; i++) g_shTable[i] = SH_SENTINEL;
    g_shDirtyCount = 0;

    return world;
}

void physWorldDestroy(PhysicsWorld *world)
{
    if (!world) return;

    // Reset static broadphase state so re-init starts clean
    g_bodyCount    = 0;
    g_shCellSize   = 0.0f;
    g_shInvCell    = 0.0f;
    g_shDirtyCount = 0;

    dfree(world, sizeof(PhysicsWorld), MEM_TAG_PHYSICS);
}

//=====================================================================================================================
// physWorldStep — fixed-timestep simulation tick
//=====================================================================================================================

void physWorldStep(PhysicsWorld *world, f32 dt)
{
    if (!world) return;
    world->accumulator += dt;

    // Spiral-of-death prevention: if accumulator falls too far behind (e.g. a
    // very slow frame), cap catchup to 5 substeps worth.  Without this, a single
    // slow frame causes 100+ substeps which makes the next frame even slower → hang.
    const f32 maxAccum = world->fixedTimestep * 5.0f;
    if (world->accumulator > maxAccum) world->accumulator = maxAccum;

    while (world->accumulator >= world->fixedTimestep)
    {
        for (u32 sub = 0; sub < world->substeps; sub++)
        {
            f32 subDt = world->fixedTimestep / (f32)world->substeps;

            // ── 1. Gravity + velocity integration (SIMD-optimized) ────────────
            // Visibility LOD: non-visible entities skip gravity on non-LOD frames.
            // SIMD velocity integration still runs on ALL entities (vel += 0 = noop
            // for skipped entities), so non-visible bodies coast naturally.
            const b8 *vis = world->visibility;
            u32 visCount  = world->visCount;
            b8 isLodFrame = (vis && world->lodInterval > 0)
                          ? (world->physFrameCounter % world->lodInterval == 0) : false;
            u32 visIdx = 0;

            for (u32 a = 0; a < world->bodyArchetypeCount; a++)
            {
                Archetype *arch = world->bodyArchetypes[a];
                PhysFieldBinding *b = &world->bindings[a];
                if (b->velX < 0 || b->posX < 0) continue;

                for (u32 c = 0; c < arch->activeChunkCount; c++)
                {
                    void **fields = getArchetypeFields(arch, c);
                    if (!fields) continue;
                    u32 count = arch->arena[c].count;

                    f32 *velX    = (f32 *)fields[b->velX];
                    f32 *velY    = (f32 *)fields[b->velY];
                    f32 *velZ    = (f32 *)fields[b->velZ];
                    f32 *frcX    = (f32 *)fields[b->forceX];
                    f32 *frcY    = (f32 *)fields[b->forceY];
                    f32 *frcZ    = (f32 *)fields[b->forceZ];
                    f32 *invMass = (f32 *)fields[b->invMass];
                    f32 *damp    = (b->damping >= 0) ? (f32 *)fields[b->damping] : NULL;
                    u32 *bt      = (b->bodyType >= 0) ? (u32 *)fields[b->bodyType] : NULL;

                    // Gravity accumulation: force += mass * gravity (per-entity for mass lookup)
                    // Non-visible entities skip gravity unless this is a LOD update frame
                    for (u32 i = 0; i < count; i++)
                    {
                        if (vis && (visIdx + i) < visCount
                            && !vis[visIdx + i] && !isLodFrame)
                            continue;
                        if (invMass[i] == 0.0f) continue;
                        if (bt && bt[i] != PHYS_BODY_DYNAMIC) continue;
                        f32 m = 1.0f / invMass[i];
                        frcX[i] += m * world->gravity.x;
                        frcY[i] += m * world->gravity.y;
                        frcZ[i] += m * world->gravity.z;
                    }

                    // Velocity integration (SIMD): vel += force * invMass * dt
                    // Create scaled invMass array: invMass * subDt
                    f32 *invMassScaled = frameAlloc(count * sizeof(f32));
                    simdMulScalar(invMass, subDt, invMassScaled, count);

                    // vel += force * (invMass * dt) using SIMD madd: out = a * b + c
                    simdMadd(frcX, invMassScaled, velX, velX, count);
                    simdMadd(frcY, invMassScaled, velY, velY, count);
                    simdMadd(frcZ, invMassScaled, velZ, velZ, count);

                    // Damping: vel *= (1 - damp * dt) using SIMD
                    if (damp)
                    {
                        f32 *dampFactor = frameAlloc(count * sizeof(f32));
                        // Compute: dampFactor[i] = 1 - damp[i] * subDt
                        simdMulScalar(damp, subDt, dampFactor, count);  // damp * dt
                        simdNeg(dampFactor, dampFactor, count);           // -(damp * dt)
                        simdAddScalar(dampFactor, 1.0f, dampFactor, count);  // 1 - damp*dt

                        // Clamp negative values to 0
                        for (u32 i = 0; i < count; i++) {
                            if (dampFactor[i] < 0.0f) dampFactor[i] = 0.0f;
                        }

                        // Apply damping: vel *= dampFactor
                        simdMul(velX, dampFactor, velX, count);
                        simdMul(velY, dampFactor, velY, count);
                        simdMul(velZ, dampFactor, velZ, count);
                    }

                    visIdx += count;
                }
            }

            // ── 2. Broadphase — spatial hash ────────────────────────────────
            buildBodyList(world);
            world->pairCount = broadphaseGetPairs(world->pairs, MAX_PHYS_PAIRS);

            // ── 3. Narrowphase — contact generation ─────────────────────────
            world->manifoldCount = 0;
            for (u32 p = 0; p < world->pairCount; p++)
            {
                CollisionPair *pair = &world->pairs[p];
                PhysFieldBinding *bA = &world->bindings[pair->archA];
                PhysFieldBinding *bB = &world->bindings[pair->archB];

                void **fA = getArchetypeFields(world->bodyArchetypes[pair->archA], pair->chunkA);
                void **fB = getArchetypeFields(world->bodyArchetypes[pair->archB], pair->chunkB);
                if (!fA || !fB) continue;

                u32 iA = pair->indexA, iB = pair->indexB;
                u32 shapeA = pair->shapeA;  // cached from broadphase — no re-inference
                u32 shapeB = pair->shapeB;

                Vec3 posA = {((f32 *)fA[bA->posX])[iA],
                              ((f32 *)fA[bA->posY])[iA],
                              ((f32 *)fA[bA->posZ])[iA]};
                Vec3 posB = {((f32 *)fB[bB->posX])[iB],
                              ((f32 *)fB[bB->posY])[iB],
                              ((f32 *)fB[bB->posZ])[iB]};

                if (world->manifoldCount >= MAX_PHYS_MANIFOLDS) break;
                ContactManifold *m = &world->manifolds[world->manifoldCount];
                m->bodyA = p; m->bodyB = p;

                b8 hit = false;
                if (shapeA == PSHAPE_SPHERE && shapeB == PSHAPE_SPHERE)
                {
                    hit = contactSphereSphere(posA, effectiveRadius(fA, bA, iA),
                                              posB, effectiveRadius(fB, bB, iB), m);
                }
                else if (shapeA == PSHAPE_SPHERE && shapeB == PSHAPE_BOX)
                {
                    hit = contactSphereBox(posA, effectiveRadius(fA, bA, iA),
                                           posB, effectiveHalf(fB, bB, iB), m);
                }
                else if (shapeA == PSHAPE_BOX && shapeB == PSHAPE_SPHERE)
                {
                    hit = contactSphereBox(posB, effectiveRadius(fB, bB, iB),
                                           posA, effectiveHalf(fA, bA, iA), m);
                    // Flip normal so it points from A toward B
                    if (hit && m->pointCount > 0)
                    {
                        m->points[0].normal.x = -m->points[0].normal.x;
                        m->points[0].normal.y = -m->points[0].normal.y;
                        m->points[0].normal.z = -m->points[0].normal.z;
                    }
                }
                else // box vs box
                {
                    hit = contactBoxBox(posA, effectiveHalf(fA, bA, iA),
                                        posB, effectiveHalf(fB, bB, iB), m);
                }

                if (hit) world->manifoldCount++;
            }

            // ── 4. Sequential impulse solver ────────────────────────────────
            #define BAUMGARTE 0.2f
            #define SLOP      0.005f

            for (u32 iter = 0; iter < world->solverIterations; iter++)
            {
                for (u32 mi = 0; mi < world->manifoldCount; mi++)
                {
                    ContactManifold *manifold = &world->manifolds[mi];
                    CollisionPair   *pair     = &world->pairs[manifold->bodyA];
                    PhysFieldBinding *bA = &world->bindings[pair->archA];
                    PhysFieldBinding *bB = &world->bindings[pair->archB];

                    void **fA = getArchetypeFields(world->bodyArchetypes[pair->archA], pair->chunkA);
                    void **fB = getArchetypeFields(world->bodyArchetypes[pair->archB], pair->chunkB);
                    if (!fA || !fB) continue;

                    u32 iA = pair->indexA, iB = pair->indexB;

                    f32 *vxA = (f32 *)fA[bA->velX]; f32 *vyA = (f32 *)fA[bA->velY]; f32 *vzA = (f32 *)fA[bA->velZ];
                    f32 *vxB = (f32 *)fB[bB->velX]; f32 *vyB = (f32 *)fB[bB->velY]; f32 *vzB = (f32 *)fB[bB->velZ];
                    f32 imA  = ((f32 *)fA[bA->invMass])[iA];
                    f32 imB  = ((f32 *)fB[bB->invMass])[iB];
                    f32 eA   = (bA->restitution >= 0) ? ((f32 *)fA[bA->restitution])[iA] : 0.4f;
                    f32 eB   = (bB->restitution >= 0) ? ((f32 *)fB[bB->restitution])[iB] : 0.4f;
                    f32 e    = eA < eB ? eA : eB;

                    u32 *btA = (bA->bodyType >= 0) ? (u32 *)fA[bA->bodyType] : NULL;
                    u32 *btB = (bB->bodyType >= 0) ? (u32 *)fB[bB->bodyType] : NULL;
                    if (imA == 0.0f && imB == 0.0f) continue;
                    if (btA && btA[iA] != PHYS_BODY_DYNAMIC) imA = 0.0f;
                    if (btB && btB[iB] != PHYS_BODY_DYNAMIC) imB = 0.0f;
                    f32 totalInvMass = imA + imB;
                    if (totalInvMass == 0.0f) continue;

                    for (u32 ci = 0; ci < manifold->pointCount; ci++)
                    {
                        ContactPoint *c = &manifold->points[ci];
                        Vec3 n = c->normal;

                        f32 rvX = vxB[iB] - vxA[iA];
                        f32 rvY = vyB[iB] - vyA[iA];
                        f32 rvZ = vzB[iB] - vzA[iA];
                        f32 rvn = rvX * n.x + rvY * n.y + rvZ * n.z;
                        if (rvn > 0.0f) continue; // separating

                        f32 j = -(1.0f + e) * rvn / totalInvMass;
                        vxA[iA] -= n.x * j * imA; vyA[iA] -= n.y * j * imA; vzA[iA] -= n.z * j * imA;
                        vxB[iB] += n.x * j * imB; vyB[iB] += n.y * j * imB; vzB[iB] += n.z * j * imB;

                        // Baumgarte position correction (pure position-space, no /dt)
                        f32 corr = c->depth - SLOP;
                        if (corr > 0.0f)
                        {
                            f32 corrMag = corr * BAUMGARTE / totalInvMass;
                            f32 *pxA = (f32 *)fA[bA->posX]; f32 *pyA = (f32 *)fA[bA->posY]; f32 *pzA = (f32 *)fA[bA->posZ];
                            f32 *pxB = (f32 *)fB[bB->posX]; f32 *pyB = (f32 *)fB[bB->posY]; f32 *pzB = (f32 *)fB[bB->posZ];
                            pxA[iA] -= n.x * corrMag * imA;
                            pyA[iA] -= n.y * corrMag * imA;
                            pzA[iA] -= n.z * corrMag * imA;
                            pxB[iB] += n.x * corrMag * imB;
                            pyB[iB] += n.y * corrMag * imB;
                            pzB[iB] += n.z * corrMag * imB;
                        }
                    }
                }
            }

            // ── 5. Position integration (SIMD-optimized) ─────────────────────
            for (u32 a = 0; a < world->bodyArchetypeCount; a++)
            {
                Archetype *arch = world->bodyArchetypes[a];
                PhysFieldBinding *b = &world->bindings[a];
                if (b->velX < 0 || b->posX < 0) continue;

                for (u32 c = 0; c < arch->activeChunkCount; c++)
                {
                    void **fields = getArchetypeFields(arch, c);
                    u32 count     = arch->arena[c].count;
                    f32 *posX  = (f32 *)fields[b->posX];
                    f32 *posY  = (f32 *)fields[b->posY];
                    f32 *posZ  = (f32 *)fields[b->posZ];
                    f32 *velX  = (f32 *)fields[b->velX];
                    f32 *velY  = (f32 *)fields[b->velY];
                    f32 *velZ  = (f32 *)fields[b->velZ];
                    f32 *frcX  = (f32 *)fields[b->forceX];
                    f32 *frcY  = (f32 *)fields[b->forceY];
                    f32 *frcZ  = (f32 *)fields[b->forceZ];

                    // Position integration (SIMD): pos += vel * dt
                    // Use MulScalar to scale velocity, then Add — avoids allocating a
                    // uniform dtArray just to broadcast a single scalar.
                    f32 *scaledVel = frameAlloc(count * sizeof(f32));

                    simdMulScalar(velX, subDt, scaledVel, count);
                    simdAdd(posX, scaledVel, posX, count);
                    simdMulScalar(velY, subDt, scaledVel, count);
                    simdAdd(posY, scaledVel, posY, count);
                    simdMulScalar(velZ, subDt, scaledVel, count);
                    simdAdd(posZ, scaledVel, posZ, count);

                    // Reset forces (simple memset for now, TODO: add simdZero)
                    memset(frcX, 0, count * sizeof(f32));
                    memset(frcY, 0, count * sizeof(f32));
                    memset(frcZ, 0, count * sizeof(f32));
                }
            }

            // ── 6. Collision callbacks ───────────────────────────────────────
            if (world->onCollision)
            {
                for (u32 mi = 0; mi < world->manifoldCount; mi++)
                {
                    CollisionPair *pair = &world->pairs[world->manifolds[mi].bodyA];
                    world->onCollision(pair->indexA, pair->indexB, &world->manifolds[mi]);
                }
            }
        }
        world->accumulator -= world->fixedTimestep;
        world->physFrameCounter++;
    }

    // Clear frame-scoped visibility (buffer is frame-allocated, will be invalid after frameReset)
    world->visibility = NULL;
    world->visCount   = 0;
}

//=====================================================================================================================
// World settings
//=====================================================================================================================

void physWorldSetGravity(PhysicsWorld *world, Vec3 gravity)     { if (world) world->gravity = gravity; }
void physWorldSetIterations(PhysicsWorld *world, u32 i)         { if (world) world->solverIterations = i; }
void physWorldSetSubsteps(PhysicsWorld *world, u32 s)           { if (world && s > 0) world->substeps = s; }
void physWorldSetCollisionCallback(PhysicsWorld *world, CollisionFn fn) { if (world) world->onCollision = fn; }
void physWorldSetTriggerCallback(PhysicsWorld *world, TriggerFn fn)     { if (world) world->onTrigger = fn; }

void physWorldSetVisibility(PhysicsWorld *world, const b8 *visible, u32 count, u32 lodInterval)
{
    if (!world) return;
    world->visibility   = visible;
    world->visCount     = count;
    world->lodInterval  = lodInterval;
}

//=====================================================================================================================
// Archetype registration
//=====================================================================================================================

void physRegisterArchetype(PhysicsWorld *world, Archetype *arch)
{
    if (!world || !arch || world->bodyArchetypeCount >= MAX_PHYS_ARCHETYPES) return;

    u32 idx = world->bodyArchetypeCount;
    FLAG_SET(arch->flags, ARCH_PHYSICS_BODY);
    world->bodyArchetypes[idx] = arch;
    world->bindings[idx] = buildBinding(arch->layout);
    world->bodyArchetypeCount++;

    PhysFieldBinding *b = &world->bindings[idx];
    if (b->posX < 0 || b->velX < 0)
        WARN("physRegisterArchetype: '%s' missing Position or LinearVelocity fields", arch->layout->name);
    if (b->radius < 0 && b->halfX < 0)
        WARN("physRegisterArchetype: '%s' has no SphereRadius or ColliderHalfX — will use scale fallback",
             arch->layout->name);
}

u32       physGetBodyArchetypeCount(PhysicsWorld *world)        { return world ? world->bodyArchetypeCount : 0; }
Archetype *physGetBodyArchetype(PhysicsWorld *world, u32 index)
{
    if (!world || index >= world->bodyArchetypeCount) return NULL;
    return world->bodyArchetypes[index];
}

//=====================================================================================================================
// Binding accessor functions — expose cached bindings for broadphase/narrowphase queries
//=====================================================================================================================

// Accessor function for cached field bindings
PhysFieldBinding *physGetBindingByIndex(PhysicsWorld *world, u32 index)
{
    if (!world || index >= world->bodyArchetypeCount) return NULL;
    return &world->bindings[index];
}

//=====================================================================================================================
// Physics DLL loading
//=====================================================================================================================

b8 loadPhysicsDLL(const c8 *dllPath, PhysicsDLL *out)
{
    if (!dllPath || !out) return false;
    memset(out, 0, sizeof(PhysicsDLL));
    if (!dllLoad(dllPath, &out->dll)) return false;

    GetPhysicsPluginFn getPlugin =
        (GetPhysicsPluginFn)dllSymbol(&out->dll, "druidGetPhysicsSystem");
    if (!getPlugin) { dllUnload(&out->dll); return false; }

    getPlugin(&out->plugin);
    if (!out->plugin.init || !out->plugin.step || !out->plugin.shutdown)
    {
        dllUnload(&out->dll); return false;
    }
    out->loaded = true;
    return true;
}

void unloadPhysicsDLL(PhysicsDLL *dll)
{
    if (!dll || !dll->loaded) return;
    dllUnload(&dll->dll);
    dll->loaded = false;
}

//=====================================================================================================================
// Global physics singleton
//=====================================================================================================================

PhysicsWorld *physicsWorld = NULL;

PhysicsWorld *physInit(Vec3 gravity, f32 timestep)
{
    if (physicsWorld) physWorldDestroy(physicsWorld);
    physicsWorld = physWorldCreate(gravity, timestep);
    return physicsWorld;
}

void physShutdown(void)
{
    if (physicsWorld) { physWorldDestroy(physicsWorld); physicsWorld = NULL; }
}

void physTick(f32 dt)
{
    if (physicsWorld) physWorldStep(physicsWorld, dt);
}

void physAutoRegisterScene(SceneData *scene)
{
    if (!physicsWorld || !scene) return;
    for (u32 i = 0; i < scene->archetypeCount; i++)
        if (FLAG_CHECK(scene->archetypes[i].flags, ARCH_PHYSICS_BODY))
            physRegisterArchetype(physicsWorld, &scene->archetypes[i]);
}
