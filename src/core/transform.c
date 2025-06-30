
#include "../../include/druid.h"

Mat4 getModel(const Transform* transform)
{
    if (!transform) return mat4Identity();
    
	Mat4 posMat = mat4Identity();
	posMat = mat4Translate(posMat, transform->pos);

    
    //rotation matrix from quaternion
    Mat4 rotMat = quatToRotationMatrix(transform->rot);
    
    //scale matrix
    Mat4 scaleMat = mat4ScaleVec(transform->scale);
    
    //combined model matrix
    Mat4 result = mat4Mul(posMat, mat4Mul(rotMat, scaleMat));
    
    return result;
	
}
