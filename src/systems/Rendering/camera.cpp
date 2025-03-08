#include "Camera.h"

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
glm::mat4 getViewProjection(const Camera* camera)
{
		return camera->projection * glm::lookAt(camera->pos, camera->pos + camera->forward, camera->up);
}

void initCamera(Camera* camera, const glm::vec3& pos, float fov, float aspect, float nearClip, float farClip)
{
    camera->pos = pos;
    camera->forward = glm::vec3(0.0f, 0.0f, 1.0f);
    camera->up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera->projection = glm::perspective(fov, aspect, nearClip, farClip);
}

void moveForward(Camera* camera, float amt)
{
   camera->pos += camera->forward * amt;
}

void moveRight(Camera* camera, float amt)
{
    camera->pos += glm::cross(camera->up, camera->forward) * amt;
}

void pitch(Camera* camera, float angle) 
{
    auto right = glm::normalize(glm::cross(camera->up, camera->forward));
    camera->forward = glm::vec3(glm::normalize(glm::rotate(angle, right) * glm::vec4(camera->forward, 0.0)));
    camera->up = glm::normalize(glm::cross(camera->forward, right));
}

void rotateY(Camera* camera, float angle)
{
    static const glm::vec3 UP(0.0f, 1.0f, 0.0f);
    auto rotation = glm::rotate(angle, UP);
    camera->forward = glm::vec3(glm::normalize(rotation * glm::vec4(camera->forward, 0.0)));
    camera->up = glm::vec3(glm::normalize(rotation * glm::vec4(camera->up, 0.0)));
    
}
