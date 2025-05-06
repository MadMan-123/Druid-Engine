#include "../../../Include/druid.h"

Mat4 getViewProjection(const Camera* camera)
{
	auto view = mat4LookAt(camera->pos, v3Add(camera->pos, camera->forward), camera->up);
	return mat4Mul(camera->projection,view);
}

void initCamera(Camera* camera, const Vec3& pos, f32 fov, f32 aspect, f32 nearClip, f32 farClip)
{
    camera->pos = pos;
    camera->forward = {0.0f, 0.0f, -1.0f};
    camera->up = {0.0f, 1.0f, 0.0f};
    camera->projection = mat4Perspective(fov, aspect, nearClip, farClip);
}

void moveForward(Camera* camera, f32 amt)
{
   	camera->pos = v3Add(camera->pos,v3Scale(camera->forward , amt));
}

void moveRight(Camera* camera, f32 amt)
{
	Vec3 right = v3Norm(v3Cross(camera->forward,camera->up));
    camera->pos = v3Add(camera->pos,v3Scale( right, amt));
}

void pitch(Camera* camera, f32 angle) 
{
	Vec3 right = v3Norm(v3Cross(camera->up, camera->forward));
	Mat4 rot = mat4Rotate(angle, right);
	
	//set the forward and up
	camera->forward = v3Norm(mat4TransformDirection(rot, camera->forward));
	camera->up = v3Norm(v3Cross(camera->forward, right));
}

void rotateY(Camera* camera, f32 angle)
{

    Mat4 rotation = mat4Rotate(angle, v3Up);

    camera->forward = v3Norm(mat4TransformDirection(rotation, camera->forward));
    camera->up = v3Norm(mat4TransformDirection(rotation, camera->up));
}
