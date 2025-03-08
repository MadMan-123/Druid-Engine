#pragma once

#include <glm/glm.hpp>
#include "../../defines.h"
typedef struct
{
	glm::mat4 projection;
	glm::vec3 pos;
	glm::vec3 forward;
	glm::vec3 up;
} Camera;

DAPI glm::mat4 getViewProjection(const Camera* camera);

DAPI void initCamera(Camera* camera, const glm::vec3& pos, float fov, float aspect, float nearClip, float farClip);

DAPI void moveForward(Camera* camera,float amt);

DAPI void moveRight(Camera* camera,float amt);

DAPI void pitch(Camera* camera, float angle);

DAPI void rotateY(Camera* camera,float angle);

