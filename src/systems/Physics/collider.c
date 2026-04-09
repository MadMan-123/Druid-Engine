
#include "../../../include/druid.h"
#include <math.h>
#include <float.h>

//=====================================================================================================================
// Internal state structs
//=====================================================================================================================

typedef struct { f32 radius; } CircleState;
typedef struct { Vec2 scale; } Box2DState;
typedef struct { Vec3 halfExtents; } BoxState;
typedef struct { f32 radius; } SphereState;
typedef struct { f32 radius; f32 halfHeight; } CylinderState;

typedef struct
{
    Vec3 *vertices;
    u32  *indices;
    u32   vertexCount;
    u32   indexCount;
    AABB  aabb;
} MeshState;

typedef struct { Mesh *mesh; Transform *transform; } LegacyMeshState;

//=====================================================================================================================
// Helpers
//=====================================================================================================================

static f32 clampf(f32 val, f32 lo, f32 hi)
{
    return val < lo ? lo : (val > hi ? hi : val);
}

static Vec3 closestPointOnTriangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 ab = v3Sub(b, a);
    Vec3 ac = v3Sub(c, a);
    Vec3 ap = v3Sub(p, a);

    f32 d1 = v3Dot(ab, ap);
    f32 d2 = v3Dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    Vec3 bp = v3Sub(p, b);
    f32 d3 = v3Dot(ab, bp);
    f32 d4 = v3Dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    f32 vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        f32 v = d1 / (d1 - d3);
        return v3Add(a, v3Scale(ab, v));
    }

    Vec3 cp = v3Sub(p, c);
    f32 d5 = v3Dot(ab, cp);
    f32 d6 = v3Dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    f32 vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        f32 w = d2 / (d2 - d6);
        return v3Add(a, v3Scale(ac, w));
    }

    f32 va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        f32 w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return v3Add(b, v3Scale(v3Sub(c, b), w));
    }

    f32 denom = 1.0f / (va + vb + vc);
    f32 v = vb * denom;
    f32 w = vc * denom;
    return v3Add(a, v3Add(v3Scale(ab, v), v3Scale(ac, w)));
}

static b8 triangleVsAABB(Vec3 v0, Vec3 v1, Vec3 v2, Vec3 center, Vec3 half)
{
    // Translate triangle to AABB center
    Vec3 a = v3Sub(v0, center);
    Vec3 b = v3Sub(v1, center);
    Vec3 c = v3Sub(v2, center);

    Vec3 ab = v3Sub(b, a);
    Vec3 bc = v3Sub(c, b);
    Vec3 ca = v3Sub(a, c);

    // 9 SAT axes: edge cross products
    Vec3 edges[3] = {ab, bc, ca};
    Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    for (i32 i = 0; i < 3; i++)
    {
        for (i32 j = 0; j < 3; j++)
        {
            Vec3 axis = v3Cross(edges[i], axes[j]);
            f32 mag = v3Dot(axis, axis);
            if (mag < 1e-8f) continue;

            f32 pa = v3Dot(a, axis);
            f32 pb = v3Dot(b, axis);
            f32 pc = v3Dot(c, axis);

            f32 triMin = pa < pb ? (pa < pc ? pa : pc) : (pb < pc ? pb : pc);
            f32 triMax = pa > pb ? (pa > pc ? pa : pc) : (pb > pc ? pb : pc);

            f32 r = half.x * fabsf(axis.x) + half.y * fabsf(axis.y) + half.z * fabsf(axis.z);
            if (triMin > r || triMax < -r) return false;
        }
    }

    // 3 box face normals
    f32 minX = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    f32 maxX = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    if (minX > half.x || maxX < -half.x) return false;

    f32 minY = a.y < b.y ? (a.y < c.y ? a.y : c.y) : (b.y < c.y ? b.y : c.y);
    f32 maxY = a.y > b.y ? (a.y > c.y ? a.y : c.y) : (b.y > c.y ? b.y : c.y);
    if (minY > half.y || maxY < -half.y) return false;

    f32 minZ = a.z < b.z ? (a.z < c.z ? a.z : c.z) : (b.z < c.z ? b.z : c.z);
    f32 maxZ = a.z > b.z ? (a.z > c.z ? a.z : c.z) : (b.z > c.z ? b.z : c.z);
    if (minZ > half.z || maxZ < -half.z) return false;

    // Triangle normal axis
    Vec3 triNorm = v3Cross(ab, bc);
    f32 d = v3Dot(triNorm, a);
    f32 r = half.x * fabsf(triNorm.x) + half.y * fabsf(triNorm.y) + half.z * fabsf(triNorm.z);
    if (fabsf(d) > r) return false;

    return true;
}

static AABB computeMeshAABB(const Vec3 *verts, u32 count)
{
    AABB box;
    if (count == 0)
    {
        box.min = (Vec3){0, 0, 0};
        box.max = (Vec3){0, 0, 0};
        return box;
    }
    box.min = verts[0];
    box.max = verts[0];
    for (u32 i = 1; i < count; i++)
    {
        if (verts[i].x < box.min.x) box.min.x = verts[i].x;
        if (verts[i].y < box.min.y) box.min.y = verts[i].y;
        if (verts[i].z < box.min.z) box.min.z = verts[i].z;
        if (verts[i].x > box.max.x) box.max.x = verts[i].x;
        if (verts[i].y > box.max.y) box.max.y = verts[i].y;
        if (verts[i].z > box.max.z) box.max.z = verts[i].z;
    }
    return box;
}

//=====================================================================================================================
// Legacy 2D creation
//=====================================================================================================================

Collider *createCircleCollider(f32 radius)
{
    Collider *col = (Collider *)dalloc(sizeof(Collider), MEM_TAG_PHYSICS);
    if (!col) return NULL;

    col->type = COLLIDER_CIRCLE;
    col->state = dalloc(sizeof(CircleState), MEM_TAG_PHYSICS);
    if (!col->state) { dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }

    ((CircleState *)col->state)->radius = radius;
    col->layer = 0;
    col->isColliding = false;
    col->isTrigger = false;
    col->response = NULL;
    col->onTrigger = NULL;
    return col;
}

Collider *createBoxCollider(Vec2 scale)
{
    Collider *col = (Collider *)dalloc(sizeof(Collider), MEM_TAG_PHYSICS);
    if (!col) return NULL;

    col->type = COLLIDER_BOX_2D;
    col->state = dalloc(sizeof(Box2DState), MEM_TAG_PHYSICS);
    if (!col->state) { dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }

    ((Box2DState *)col->state)->scale = scale;
    col->layer = 0;
    col->isColliding = false;
    col->isTrigger = false;
    col->response = NULL;
    col->onTrigger = NULL;
    return col;
}

//=====================================================================================================================
// 3D creation
//=====================================================================================================================

Collider *createSphereCollider(f32 radius)
{
    Collider *col = (Collider *)dalloc(sizeof(Collider), MEM_TAG_PHYSICS);
    if (!col) return NULL;

    col->type = COLLIDER_SPHERE;
    col->state = dalloc(sizeof(SphereState), MEM_TAG_PHYSICS);
    if (!col->state) { dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }

    ((SphereState *)col->state)->radius = radius;
    col->layer = 0;
    col->isColliding = false;
    col->isTrigger = false;
    col->response = NULL;
    col->onTrigger = NULL;
    return col;
}

Collider *createCubeCollider(Vec3 halfExtents)
{
    Collider *col = (Collider *)dalloc(sizeof(Collider), MEM_TAG_PHYSICS);
    if (!col) return NULL;

    col->type = COLLIDER_BOX;
    col->state = dalloc(sizeof(BoxState), MEM_TAG_PHYSICS);
    if (!col->state) { dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }

    ((BoxState *)col->state)->halfExtents = halfExtents;
    col->layer = 0;
    col->isColliding = false;
    col->isTrigger = false;
    col->response = NULL;
    col->onTrigger = NULL;
    return col;
}

Collider *createCylinderCollider(f32 radius, f32 halfHeight)
{
    Collider *col = (Collider *)dalloc(sizeof(Collider), MEM_TAG_PHYSICS);
    if (!col) return NULL;

    col->type = COLLIDER_CYLINDER;
    col->state = dalloc(sizeof(CylinderState), MEM_TAG_PHYSICS);
    if (!col->state) { dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }

    CylinderState *s = (CylinderState *)col->state;
    s->radius = radius;
    s->halfHeight = halfHeight;
    col->layer = 0;
    col->isColliding = false;
    col->isTrigger = false;
    col->response = NULL;
    col->onTrigger = NULL;
    return col;
}

Collider *createMeshCollider3D(const Vec3 *vertices, const u32 *indices,
                               u32 vertexCount, u32 indexCount)
{
    if (!vertices || vertexCount == 0) return NULL;

    Collider *col = (Collider *)dalloc(sizeof(Collider), MEM_TAG_PHYSICS);
    if (!col) return NULL;

    col->type = COLLIDER_MESH;
    MeshState *ms = (MeshState *)dalloc(sizeof(MeshState), MEM_TAG_PHYSICS);
    if (!ms) { dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }

    ms->vertexCount = vertexCount;
    ms->indexCount = indexCount;

    ms->vertices = (Vec3 *)dalloc(sizeof(Vec3) * vertexCount, MEM_TAG_PHYSICS);
    if (!ms->vertices) { dfree(ms, sizeof(MeshState), MEM_TAG_PHYSICS); dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }
    memcpy(ms->vertices, vertices, sizeof(Vec3) * vertexCount);

    if (indices && indexCount > 0)
    {
        ms->indices = (u32 *)dalloc(sizeof(u32) * indexCount, MEM_TAG_PHYSICS);
        if (!ms->indices) { dfree(ms->vertices, sizeof(Vec3) * vertexCount, MEM_TAG_PHYSICS); dfree(ms, sizeof(MeshState), MEM_TAG_PHYSICS); dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }
        memcpy(ms->indices, indices, sizeof(u32) * indexCount);
    }
    else
    {
        ms->indices = NULL;
        ms->indexCount = 0;
    }

    ms->aabb = computeMeshAABB(vertices, vertexCount);
    col->state = ms;
    col->layer = 0;
    col->isColliding = false;
    col->isTrigger = false;
    col->response = NULL;
    col->onTrigger = NULL;
    return col;
}

Collider *createMeshCollider(Mesh *mesh, Transform *transform)
{
    Collider *col = (Collider *)dalloc(sizeof(Collider), MEM_TAG_PHYSICS);
    if (!col) return NULL;

    col->type = COLLIDER_MESH;
    LegacyMeshState *ls = (LegacyMeshState *)dalloc(sizeof(LegacyMeshState), MEM_TAG_PHYSICS);
    if (!ls) { dfree(col, sizeof(Collider), MEM_TAG_PHYSICS); return NULL; }

    ls->mesh = mesh;
    ls->transform = transform;
    col->state = ls;
    col->layer = 0;
    col->isColliding = false;
    col->isTrigger = false;
    col->response = NULL;
    col->onTrigger = NULL;
    return col;
}

//=====================================================================================================================
// Utilities
//=====================================================================================================================

b8 cleanCollider(Collider *col)
{
    if (!col) return false;
    if (col->state)
    {
        if (col->type == COLLIDER_MESH)
        {
            MeshState *ms = (MeshState *)col->state;
            if (ms->vertices) dfree(ms->vertices, sizeof(Vec3) * ms->vertexCount, MEM_TAG_PHYSICS);
            if (ms->indices) dfree(ms->indices, sizeof(u32) * ms->indexCount, MEM_TAG_PHYSICS);
            dfree(col->state, sizeof(MeshState), MEM_TAG_PHYSICS);
        }
        else
        {
            // All other collider states are small fixed-size structs
            dfree(col->state, 0, MEM_TAG_PHYSICS);
        }
    }
    dfree(col, sizeof(Collider), MEM_TAG_PHYSICS);
    return true;
}

f32 getRadius(Collider *col)
{
    if (!col || !col->state) return 0.0f;
    if (col->type == COLLIDER_CIRCLE)
        return ((CircleState *)col->state)->radius;
    if (col->type == COLLIDER_SPHERE)
        return ((SphereState *)col->state)->radius;
    if (col->type == COLLIDER_CYLINDER)
        return ((CylinderState *)col->state)->radius;
    return 0.0f;
}

Vec2 getScale(Collider *col)
{
    if (!col || !col->state) return (Vec2){0, 0};
    if (col->type == COLLIDER_BOX_2D)
        return ((Box2DState *)col->state)->scale;
    return (Vec2){0, 0};
}

Vec3 getHalfExtents(Collider *col)
{
    if (!col || !col->state) return (Vec3){0, 0, 0};
    if (col->type == COLLIDER_BOX)
        return ((BoxState *)col->state)->halfExtents;
    return (Vec3){0, 0, 0};
}

b8 setBoxScale(Collider *col, Vec2 scale)
{
    if (!col || !col->state) return false;
    if (col->type == COLLIDER_BOX_2D)
    {
        ((Box2DState *)col->state)->scale = scale;
        return true;
    }
    return false;
}

AABB colliderComputeAABB(Collider *col, Vec3 pos)
{
    AABB box = {{0, 0, 0}, {0, 0, 0}};
    if (!col || !col->state) return box;

    switch (col->type)
    {
    case COLLIDER_SPHERE:
    {
        f32 r = ((SphereState *)col->state)->radius;
        box.min = (Vec3){pos.x - r, pos.y - r, pos.z - r};
        box.max = (Vec3){pos.x + r, pos.y + r, pos.z + r};
    } break;
    case COLLIDER_BOX:
    {
        Vec3 h = ((BoxState *)col->state)->halfExtents;
        box.min = v3Sub(pos, h);
        box.max = v3Add(pos, h);
    } break;
    case COLLIDER_CYLINDER:
    {
        CylinderState *cs = (CylinderState *)col->state;
        box.min = (Vec3){pos.x - cs->radius, pos.y - cs->halfHeight, pos.z - cs->radius};
        box.max = (Vec3){pos.x + cs->radius, pos.y + cs->halfHeight, pos.z + cs->radius};
    } break;
    case COLLIDER_MESH:
    {
        MeshState *ms = (MeshState *)col->state;
        box.min = v3Add(ms->aabb.min, pos);
        box.max = v3Add(ms->aabb.max, pos);
    } break;
    case COLLIDER_CIRCLE:
    {
        f32 r = ((CircleState *)col->state)->radius;
        box.min = (Vec3){pos.x - r, pos.y - r, 0};
        box.max = (Vec3){pos.x + r, pos.y + r, 0};
    } break;
    case COLLIDER_BOX_2D:
    {
        Vec2 s = ((Box2DState *)col->state)->scale;
        box.min = (Vec3){pos.x, pos.y, 0};
        box.max = (Vec3){pos.x + s.x, pos.y + s.y, 0};
    } break;
    }
    return box;
}

b8 isAABBOverlapping(AABB a, AABB b)
{
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
           (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

//=====================================================================================================================
// Legacy 2D collision
//=====================================================================================================================

b8 isCircleColliding(Vec2 posA, f32 radA, Vec2 posB, f32 radB)
{
    return v2Mag(v2Sub(posB, posA)) <= (radA + radB);
}

b8 isBoxColliding(Vec2 posA, Vec2 scaleA, Vec2 posB, Vec2 scaleB)
{
    return (posA.x + scaleA.x >= posB.x && posB.x + scaleB.x >= posA.x) &&
           (posA.y + scaleA.y >= posB.y && posB.y + scaleB.y >= posA.y);
}

//=====================================================================================================================
// 3D collision: Sphere
//=====================================================================================================================

b8 isSphereVsSphere(Vec3 posA, f32 radA, Vec3 posB, f32 radB)
{
    Vec3 d = v3Sub(posB, posA);
    f32 distSq = v3Dot(d, d);
    f32 rSum = radA + radB;
    return distSq <= rSum * rSum;
}

b8 isSphereVsBox(Vec3 spherePos, f32 radius, Vec3 boxPos, Vec3 boxHalf)
{
    Vec3 closest;
    closest.x = clampf(spherePos.x, boxPos.x - boxHalf.x, boxPos.x + boxHalf.x);
    closest.y = clampf(spherePos.y, boxPos.y - boxHalf.y, boxPos.y + boxHalf.y);
    closest.z = clampf(spherePos.z, boxPos.z - boxHalf.z, boxPos.z + boxHalf.z);

    Vec3 d = v3Sub(spherePos, closest);
    return v3Dot(d, d) <= radius * radius;
}

b8 isSphereVsCylinder(Vec3 spherePos, f32 sphereRad,
                      Vec3 cylPos, f32 cylRad, f32 cylHalfH)
{
    // Clamp sphere center to cylinder volume, then distance check
    f32 closestY = clampf(spherePos.y, cylPos.y - cylHalfH, cylPos.y + cylHalfH);
    f32 dx = spherePos.x - cylPos.x;
    f32 dz = spherePos.z - cylPos.z;
    f32 radialDistSq = dx * dx + dz * dz;

    // Clamp to cylinder radius on XZ plane
    f32 radialDist = sqrtf(radialDistSq);
    f32 clampedRadial = radialDist < cylRad ? radialDist : cylRad;

    Vec3 closest;
    if (radialDist > 1e-8f)
    {
        f32 scale = clampedRadial / radialDist;
        closest.x = cylPos.x + dx * scale;
        closest.z = cylPos.z + dz * scale;
    }
    else
    {
        closest.x = cylPos.x;
        closest.z = cylPos.z;
    }
    closest.y = closestY;

    Vec3 d = v3Sub(spherePos, closest);
    return v3Dot(d, d) <= sphereRad * sphereRad;
}

//=====================================================================================================================
// 3D collision: Box (AABB)
//=====================================================================================================================

b8 isBoxVsBox(Vec3 posA, Vec3 halfA, Vec3 posB, Vec3 halfB)
{
    return (fabsf(posA.x - posB.x) <= halfA.x + halfB.x) &&
           (fabsf(posA.y - posB.y) <= halfA.y + halfB.y) &&
           (fabsf(posA.z - posB.z) <= halfA.z + halfB.z);
}

b8 isBoxVsCylinder(Vec3 boxPos, Vec3 boxHalf,
                   Vec3 cylPos, f32 cylRad, f32 cylHalfH)
{
    // Y axis overlap
    if (fabsf(boxPos.y - cylPos.y) > boxHalf.y + cylHalfH)
        return false;

    // XZ: closest point on box to cylinder axis
    f32 closestX = clampf(cylPos.x, boxPos.x - boxHalf.x, boxPos.x + boxHalf.x);
    f32 closestZ = clampf(cylPos.z, boxPos.z - boxHalf.z, boxPos.z + boxHalf.z);

    f32 dx = closestX - cylPos.x;
    f32 dz = closestZ - cylPos.z;
    return (dx * dx + dz * dz) <= cylRad * cylRad;
}

//=====================================================================================================================
// 3D collision: Cylinder
//=====================================================================================================================

b8 isCylinderVsCylinder(Vec3 posA, f32 radA, f32 halfHA,
                        Vec3 posB, f32 radB, f32 halfHB)
{
    // Y axis overlap
    if (fabsf(posA.y - posB.y) > halfHA + halfHB)
        return false;

    // XZ circle overlap
    f32 dx = posA.x - posB.x;
    f32 dz = posA.z - posB.z;
    f32 rSum = radA + radB;
    return (dx * dx + dz * dz) <= rSum * rSum;
}

//=====================================================================================================================
// 3D collision: Mesh
//=====================================================================================================================

b8 isMeshVsSphere(Collider *meshCol, Vec3 meshPos, Vec3 spherePos, f32 sphereRad)
{
    if (!meshCol || meshCol->type != COLLIDER_MESH || !meshCol->state) return false;

    MeshState *ms = (MeshState *)meshCol->state;
    if (!ms->vertices) return false;

    f32 radSq = sphereRad * sphereRad;

    if (ms->indices && ms->indexCount >= 3)
    {
        for (u32 i = 0; i + 2 < ms->indexCount; i += 3)
        {
            Vec3 a = v3Add(ms->vertices[ms->indices[i + 0]], meshPos);
            Vec3 b = v3Add(ms->vertices[ms->indices[i + 1]], meshPos);
            Vec3 c = v3Add(ms->vertices[ms->indices[i + 2]], meshPos);
            Vec3 cp = closestPointOnTriangle(spherePos, a, b, c);
            Vec3 d = v3Sub(spherePos, cp);
            if (v3Dot(d, d) <= radSq) return true;
        }
    }
    else
    {
        for (u32 i = 0; i + 2 < ms->vertexCount; i += 3)
        {
            Vec3 a = v3Add(ms->vertices[i + 0], meshPos);
            Vec3 b = v3Add(ms->vertices[i + 1], meshPos);
            Vec3 c = v3Add(ms->vertices[i + 2], meshPos);
            Vec3 cp = closestPointOnTriangle(spherePos, a, b, c);
            Vec3 d = v3Sub(spherePos, cp);
            if (v3Dot(d, d) <= radSq) return true;
        }
    }
    return false;
}

b8 isMeshVsBox(Collider *meshCol, Vec3 meshPos, Vec3 boxPos, Vec3 boxHalf)
{
    if (!meshCol || meshCol->type != COLLIDER_MESH || !meshCol->state) return false;

    MeshState *ms = (MeshState *)meshCol->state;
    if (!ms->vertices) return false;

    if (ms->indices && ms->indexCount >= 3)
    {
        for (u32 i = 0; i + 2 < ms->indexCount; i += 3)
        {
            Vec3 a = v3Add(ms->vertices[ms->indices[i + 0]], meshPos);
            Vec3 b = v3Add(ms->vertices[ms->indices[i + 1]], meshPos);
            Vec3 c = v3Add(ms->vertices[ms->indices[i + 2]], meshPos);
            if (triangleVsAABB(a, b, c, boxPos, boxHalf)) return true;
        }
    }
    else
    {
        for (u32 i = 0; i + 2 < ms->vertexCount; i += 3)
        {
            Vec3 a = v3Add(ms->vertices[i + 0], meshPos);
            Vec3 b = v3Add(ms->vertices[i + 1], meshPos);
            Vec3 c = v3Add(ms->vertices[i + 2], meshPos);
            if (triangleVsAABB(a, b, c, boxPos, boxHalf)) return true;
        }
    }
    return false;
}

//=====================================================================================================================
// Generic dispatch
//=====================================================================================================================

b8 collidersOverlap(Collider *a, Vec3 posA, Collider *b, Vec3 posB)
{
    if (!a || !b || !a->state || !b->state) return false;

    ColliderType ta = a->type;
    ColliderType tb = b->type;

    // Sphere vs Sphere
    if (ta == COLLIDER_SPHERE && tb == COLLIDER_SPHERE)
        return isSphereVsSphere(posA, ((SphereState *)a->state)->radius,
                                posB, ((SphereState *)b->state)->radius);

    // Box vs Box
    if (ta == COLLIDER_BOX && tb == COLLIDER_BOX)
        return isBoxVsBox(posA, ((BoxState *)a->state)->halfExtents,
                          posB, ((BoxState *)b->state)->halfExtents);

    // Cylinder vs Cylinder
    if (ta == COLLIDER_CYLINDER && tb == COLLIDER_CYLINDER)
    {
        CylinderState *ca = (CylinderState *)a->state;
        CylinderState *cb = (CylinderState *)b->state;
        return isCylinderVsCylinder(posA, ca->radius, ca->halfHeight,
                                    posB, cb->radius, cb->halfHeight);
    }

    // Sphere vs Box (either order)
    if (ta == COLLIDER_SPHERE && tb == COLLIDER_BOX)
        return isSphereVsBox(posA, ((SphereState *)a->state)->radius,
                             posB, ((BoxState *)b->state)->halfExtents);
    if (ta == COLLIDER_BOX && tb == COLLIDER_SPHERE)
        return isSphereVsBox(posB, ((SphereState *)b->state)->radius,
                             posA, ((BoxState *)a->state)->halfExtents);

    // Sphere vs Cylinder (either order)
    if (ta == COLLIDER_SPHERE && tb == COLLIDER_CYLINDER)
    {
        CylinderState *cs = (CylinderState *)b->state;
        return isSphereVsCylinder(posA, ((SphereState *)a->state)->radius,
                                  posB, cs->radius, cs->halfHeight);
    }
    if (ta == COLLIDER_CYLINDER && tb == COLLIDER_SPHERE)
    {
        CylinderState *cs = (CylinderState *)a->state;
        return isSphereVsCylinder(posB, ((SphereState *)b->state)->radius,
                                  posA, cs->radius, cs->halfHeight);
    }

    // Box vs Cylinder (either order)
    if (ta == COLLIDER_BOX && tb == COLLIDER_CYLINDER)
    {
        CylinderState *cs = (CylinderState *)b->state;
        return isBoxVsCylinder(posA, ((BoxState *)a->state)->halfExtents,
                               posB, cs->radius, cs->halfHeight);
    }
    if (ta == COLLIDER_CYLINDER && tb == COLLIDER_BOX)
    {
        CylinderState *cs = (CylinderState *)a->state;
        return isBoxVsCylinder(posB, ((BoxState *)b->state)->halfExtents,
                               posA, cs->radius, cs->halfHeight);
    }

    // Mesh vs Sphere (either order)
    if (ta == COLLIDER_MESH && tb == COLLIDER_SPHERE)
        return isMeshVsSphere(a, posA, posB, ((SphereState *)b->state)->radius);
    if (ta == COLLIDER_SPHERE && tb == COLLIDER_MESH)
        return isMeshVsSphere(b, posB, posA, ((SphereState *)a->state)->radius);

    // Mesh vs Box (either order)
    if (ta == COLLIDER_MESH && tb == COLLIDER_BOX)
        return isMeshVsBox(a, posA, posB, ((BoxState *)b->state)->halfExtents);
    if (ta == COLLIDER_BOX && tb == COLLIDER_MESH)
        return isMeshVsBox(b, posB, posA, ((BoxState *)a->state)->halfExtents);


    
    // Fallback: AABB overlap
    AABB aabbA = colliderComputeAABB(a, posA);
    AABB aabbB = colliderComputeAABB(b, posB);
    return isAABBOverlapping(aabbA, aabbB);
}
