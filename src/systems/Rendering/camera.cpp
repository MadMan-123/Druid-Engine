#include "../../../Include/druid.h"
#include <cmath> //for cosf

Mat4 getViewProjection(const Camera* camera)
{
    Vec3 forward = quatTransform(camera->orientation, v3Forward);
    Vec3 up      = quatTransform(camera->orientation, v3Up);

    Vec3 target = v3Add(camera->pos, forward);
    Mat4 view = mat4LookAt(camera->pos, target, up);
    return mat4Mul(camera->projection, view);
}
Mat4 getView(const Camera* camera, bool removeTranslation = false) 
{
    Vec3 forward = quatTransform(camera->orientation, v3Forward);
    Vec3 up = quatTransform(camera->orientation, v3Up);
    Vec3 target = v3Add(camera->pos, forward);
    
    Mat4 view = mat4LookAt(camera->pos, target, up);
    
    if (removeTranslation) 
{
        // Proper way to remove translation while preserving rotation
        Mat3 rotationPart = mat4ToMat3(view);
        view = mat3ToMat4(rotationPart);
    }
    
    return view;
}

void initCamera(Camera* camera, const Vec3& pos, f32 fov, f32 aspect, f32 nearClip, f32 farClip)
{
      camera->pos = pos;
    camera->orientation = quatIdentity();
    
    camera->projection = mat4Perspective(fov, aspect, nearClip, farClip);
}

void moveForward(Camera* camera, f32 amt)
{
    	// Get the forward vector
		Vec3 forward = quatTransform(camera->orientation, v3Forward);			

	
	    camera->pos = v3Add(camera->pos, v3Scale(forward, amt));
}


void moveRight(Camera* camera, f32 amt)
{
	// Get the right vector
	Vec3 right = quatTransform(camera->orientation, v3Right);
	
    camera->pos = v3Add(camera->pos, v3Scale(right, amt));

}


void pitch(Camera* camera, f32 angle) 
{
    // Get the right vector
	Vec3 right = quatTransform(camera->orientation, v3Right);

    // Create pitch quaternion based on the right vector
    Vec4 pitchQuat = quatFromAxisAngle(right, radians(angle));
	

    // Apply the pitch quaternion to the camera's orientation
    camera->orientation = quatNormalize(quatMul(camera->orientation, pitchQuat));

}

void rotateY(Camera* camera, f32 angle) {
    // Create yaw quaternion based on the world-up vector
    Vec4 yawQuat = quatFromAxisAngle(v3Up, radians(angle));


    // Apply the yaw quaternion to the camera's orientation
    camera->orientation = quatNormalize(quatMul(camera->orientation, yawQuat));

  

}
