﻿#include "../../../Include/druid.h"

Mat4 getViewProjection(const Camera* camera)
{
    Vec3 forward = quatTransform(camera->orientation, v3Forward);
    Vec3 up = quatTransform(camera->orientation, v3Up);
    Vec3 target = v3Add(camera->pos, forward);
    
    Mat4 view = mat4LookAt(camera->pos, target, up);
    
    return mat4Mul(camera->projection, view);
}
Mat4 getView(const Camera* camera, bool removeTranslation) 
{
    // Get the forward and up vectors
    Vec3 forward = quatTransform(camera->orientation, v3Forward);
    Vec3 up = quatTransform(camera->orientation, v3Up);
    // Get the target position
    Vec3 target = v3Add(camera->pos, forward);
    // Calculate the view matrix
    Mat4 view = mat4LookAt(camera->pos, target, up);
    // If removeTranslation is true, we need to remove the translation part of the view matrix
    if (removeTranslation) 
    {
        Mat3 rotationPart = mat4ToMat3(view);
        view = mat3ToMat4(rotationPart);
    }
    
    return view;
}

void initCamera(Camera* camera, const Vec3 pos, f32 fov, f32 aspect, f32 nearClip, f32 farClip)
{
    //set the camera position
    camera->pos = pos;
    //set the camera orientation
    camera->orientation = quatIdentity();
    //set the cameras projection 
    //todo: 3D and 2D options
    camera->projection = mat4Perspective(fov, aspect, nearClip, farClip);
}

void moveForward(Camera* camera, f32 amt)
{
    	// Get the forward vector
		Vec3 forward = quatTransform(camera->orientation, v3Forward);			
        // Move the camera forward
	    camera->pos = v3Add(camera->pos, v3Scale(forward, amt));
}


void moveRight(Camera* camera, f32 amt)
{
	//get the right vector
	Vec3 right = quatTransform(camera->orientation, v3Right);
	//move the camera right
    camera->pos = v3Add(camera->pos, v3Scale(right, amt));
}


void pitch(Camera* camera, f32 angle) 
{
    //get the right vector
	Vec3 right = quatTransform(camera->orientation, v3Right);

    //create pitch quaternion based on the right vector
    Vec4 pitchQuat = quatFromAxisAngle(right, radians(angle));
	

    //apply the pitch quaternion to the camera's orientation
    camera->orientation = quatNormalize(quatMul(camera->orientation, pitchQuat));

}

void rotateY(Camera* camera, f32 angle) {
    //create yaw quaternion based on the world-up vector
    Vec4 yawQuat = quatFromAxisAngle(v3Up, radians(angle));


    //apply the yaw quaternion to the camera's orientation
    camera->orientation = quatNormalize(quatMul(camera->orientation, yawQuat));

  

}
