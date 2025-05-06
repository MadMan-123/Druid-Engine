
#include "../../include/druid.h"
#include <glm/gtx/transform.hpp>

#include <glm/glm.hpp>
Mat4 getModel(const Transform* transform)
{
	glm::vec3 pos = { transform->pos.x, transform->pos.y, transform->pos.z };
	glm::vec3 scale = { transform->scale.x, transform->scale.y, transform->scale.z };
	glm::mat4 posMat = glm::translate(pos);
	glm::mat4 scaleMat = glm::scale(scale);
	glm::mat4 rotX = rotate(transform->rot.x, glm::vec3(1.0, 0.0, 0.0));
	glm::mat4 rotY = rotate(transform->rot.y, glm::vec3(0.0, 1.0, 0.0));
	glm::mat4 rotZ = rotate(transform->rot.z, glm::vec3(0.0, 0.0, 1.0));
	glm::mat4 rotMat = rotX * rotY * rotZ;
	glm::mat4 result = posMat * rotMat * scaleMat;
	Mat4 output = {0};
	
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            output.m[i][j] = result[i][j];
        }
    }
	return output;
	
}
