
#include "../../../include/druid.h"

//=====================================================================================================================
// Handle-based collider API (PhysicsWorld-managed)
//
// The contact solver itself runs inline inside physWorldStep (physics.c).
// These functions provide the handle-based collider creation/management API
// declared in druid.h. Currently only sphere colliders are used (via the
// SphereRadius archetype field), so these are minimal stubs.
//=====================================================================================================================

u32 physColliderCreateSphere(PhysicsWorld *world, f32 radius)
{
    (void)world; (void)radius;
    WARN("physColliderCreateSphere: handle-based colliders not yet implemented, use SphereRadius field");
    return (u32)-1;
}

u32 physColliderCreateBox(PhysicsWorld *world, Vec3 halfExtents)
{
    (void)world; (void)halfExtents;
    WARN("physColliderCreateBox: handle-based colliders not yet implemented");
    return (u32)-1;
}

u32 physColliderCreateCylinder(PhysicsWorld *world, f32 radius, f32 halfHeight)
{
    (void)world; (void)radius; (void)halfHeight;
    WARN("physColliderCreateCylinder: handle-based colliders not yet implemented");
    return (u32)-1;
}

u32 physColliderCreateMesh(PhysicsWorld *world, const Vec3 *verts, const u32 *indices,
                           u32 vertCount, u32 idxCount)
{
    (void)world; (void)verts; (void)indices; (void)vertCount; (void)idxCount;
    WARN("physColliderCreateMesh: handle-based colliders not yet implemented");
    return (u32)-1;
}

void physColliderDestroy(PhysicsWorld *world, u32 handle)
{
    (void)world; (void)handle;
}

void physColliderSetOffset(PhysicsWorld *world, u32 handle, Vec3 offset)
{
    (void)world; (void)handle; (void)offset;
}

void physColliderSetTrigger(PhysicsWorld *world, u32 handle, b8 isTrigger)
{
    (void)world; (void)handle; (void)isTrigger;
}

void physColliderSetMaterial(PhysicsWorld *world, u32 handle, PhysMaterial mat)
{
    (void)world; (void)handle; (void)mat;
}

AABB physColliderComputeAABB(PhysicsWorld *world, u32 handle, Vec3 pos, Vec4 rot)
{
    (void)world; (void)handle; (void)rot;
    // Default: point AABB at position
    return (AABB){pos, pos};
}
