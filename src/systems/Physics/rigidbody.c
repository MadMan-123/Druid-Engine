
#include "../../../include/druid.h"


static i32 fieldIdx(StructLayout *layout, const c8 *name)
{
    for (u32 i = 0; i < layout->count; i++)
    {
        if (strcmp(layout->fields[i].name, name) == 0)
            return (i32)i;
    }
    return -1;
}

void physBodyApplyForce(PhysicsWorld *world, Archetype *arch, u32 index, Vec3 force)
{
    if (!world || !arch) return;
    (void)world;

    i32 fxI = fieldIdx(arch->layout, "ForceX");
    i32 fyI = fieldIdx(arch->layout, "ForceY");
    i32 fzI = fieldIdx(arch->layout, "ForceZ");
    if (fxI < 0 || fyI < 0 || fzI < 0) return;

    u32 chunkIdx = index / arch->chunkCapacity;
    u32 localIdx = index % arch->chunkCapacity;

    void **fields = getArchetypeFields(arch, chunkIdx);
    if (!fields) return;

    ((f32 *)fields[fxI])[localIdx] += force.x;
    ((f32 *)fields[fyI])[localIdx] += force.y;
    ((f32 *)fields[fzI])[localIdx] += force.z;
}

void physBodyApplyTorque(PhysicsWorld *world, Archetype *arch, u32 index, Vec3 torque)
{
    // No angular dynamics — intentional no-op
    (void)world; (void)arch; (void)index; (void)torque;
}

void physBodyApplyImpulse(PhysicsWorld *world, Archetype *arch, u32 index, Vec3 impulse)
{
    if (!world || !arch) return;
    (void)world;

    i32 vxI = fieldIdx(arch->layout, "LinearVelocityX");
    i32 vyI = fieldIdx(arch->layout, "LinearVelocityY");
    i32 vzI = fieldIdx(arch->layout, "LinearVelocityZ");
    i32 imI = fieldIdx(arch->layout, "InvMass");
    if (vxI < 0 || vyI < 0 || vzI < 0 || imI < 0) return;

    u32 chunkIdx = index / arch->chunkCapacity;
    u32 localIdx = index % arch->chunkCapacity;

    void **fields = getArchetypeFields(arch, chunkIdx);
    if (!fields) return;

    f32 im = ((f32 *)fields[imI])[localIdx];
    ((f32 *)fields[vxI])[localIdx] += impulse.x * im;
    ((f32 *)fields[vyI])[localIdx] += impulse.y * im;
    ((f32 *)fields[vzI])[localIdx] += impulse.z * im;
}

void physBodyApplyImpulseAt(PhysicsWorld *world, Archetype *arch, u32 index,
                            Vec3 impulse, Vec3 point)
{
    // Linear impulse only (angular requires inertia tensor — future work)
    physBodyApplyImpulse(world, arch, index, impulse);
    (void)point;
}
