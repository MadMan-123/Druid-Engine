#include "../../include/druid.h"

// Transform utilities for Druid engine
// Provides model matrix construction for entities (column-major, OpenGL order)

Mat4 getModel(const Transform* transform)
{
    if (!transform) return mat4Identity();
    
    Mat4 scaleMat = mat4ScaleVec(transform->scale);
    Mat4 rotMat = quatToRotationMatrix(transform->rot);
    Mat4 transMat = mat4Translate(mat4Identity(), transform->pos);
    Mat4 result = mat4Mul(transMat, mat4Mul(rotMat, scaleMat));
    
    return result;
}