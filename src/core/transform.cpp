
#include "../../include/druid.h"
#include <glm/gtx/transform.hpp>

#include <glm/glm.hpp>
Mat4 getModel(const Transform* transform)
{
if (!transform) return mat4Identity();
    
    // Position matrix
    Mat4 posMat = mat4Translate(transform->pos);
    
    // Rotation matrix from quaternion
    Mat4 rotMat = quatToRotationMatrix(transform->rot);
    
    // Scale matrix
    Mat4 scaleMat = mat4ScaleVec(transform->scale);
    
    // Combined model matrix: Position * Rotation * Scale
    Mat4 result = mat4Mul(posMat, mat4Mul(rotMat, scaleMat));
    
    return result;
	
}
