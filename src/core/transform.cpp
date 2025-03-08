#include "Transform.h"

#include <glm/gtx/transform.hpp>

glm::mat4 getModel(const Transform* transform)
{
	glm::mat4 posMat = translate(transform->pos);
	glm::mat4 scaleMat = scale(transform->scale);
	glm::mat4 rotX = rotate(transform->rot.x, glm::vec3(1.0, 0.0, 0.0));
	glm::mat4 rotY = rotate(transform->rot.y, glm::vec3(0.0, 1.0, 0.0));
	glm::mat4 rotZ = rotate(transform->rot.z, glm::vec3(0.0, 0.0, 1.0));
	glm::mat4 rotMat = rotX * rotY * rotZ;
	return posMat * rotMat * scaleMat;
}
