#pragma once

#include <glm/glm.hpp>
#include "../defines.h"

typedef struct
{
	glm::vec3 pos;
	glm::vec3 rot;
	glm::vec3 scale;
} Transform;



DAPI glm::mat4 getModel(const Transform* transform);


	

