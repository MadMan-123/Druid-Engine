#include "../../include/druid.h"

Mat4 getModel(const Transform* transform)
{
    if (!transform) return mat4Identity();

    // Build TRS matrix directly: T * R * S in ~34 ops instead of ~230.
    // Quaternion rotation elements
    const f32 qx = transform->rot.x;
    const f32 qy = transform->rot.y;
    const f32 qz = transform->rot.z;
    const f32 qw = transform->rot.w;

    const f32 xx = qx * qx;
    const f32 xy = qx * qy;
    const f32 xz = qx * qz;
    const f32 xw = qx * qw;
    const f32 yy = qy * qy;
    const f32 yz = qy * qz;
    const f32 yw = qy * qw;
    const f32 zz = qz * qz;
    const f32 zw = qz * qw;

    // Scale factors
    const f32 sx = transform->scale.x;
    const f32 sy = transform->scale.y;
    const f32 sz = transform->scale.z;

    // Column 0: rotation column 0 * sx
    Mat4 result;
    result.m[0][0] = (1.0f - 2.0f * (yy + zz)) * sx;
    result.m[0][1] = (2.0f * (xy - zw))         * sx;
    result.m[0][2] = (2.0f * (xz + yw))         * sx;
    result.m[0][3] = 0.0f;

    // Column 1: rotation column 1 * sy
    result.m[1][0] = (2.0f * (xy + zw))         * sy;
    result.m[1][1] = (1.0f - 2.0f * (xx + zz)) * sy;
    result.m[1][2] = (2.0f * (yz - xw))         * sy;
    result.m[1][3] = 0.0f;

    // Column 2: rotation column 2 * sz
    result.m[2][0] = (2.0f * (xz - yw))         * sz;
    result.m[2][1] = (2.0f * (yz + xw))         * sz;
    result.m[2][2] = (1.0f - 2.0f * (xx + yy)) * sz;
    result.m[2][3] = 0.0f;

    // Column 3: translation
    result.m[3][0] = transform->pos.x;
    result.m[3][1] = transform->pos.y;
    result.m[3][2] = transform->pos.z;
    result.m[3][3] = 1.0f;

    return result;
}