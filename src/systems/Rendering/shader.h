#pragma once
#include <string>
#include <GL\glew.h>
#include "../../Core/transform.h"
#include "camera.h"

typedef struct
{
	enum
	{
		NONE = -1,
		VERTEX_SHADER,
		FRAGMENT_SHADER,
		NUM_SHADERS
	};
	
	enum
	{
		TRANSFORM_U,

		NUM_UNIFORMS
	};

	GLuint program; // Track the shader program
	GLuint shaders[NUM_SHADERS]; //array of shaders
	GLuint uniforms[NUM_UNIFORMS]; //no of uniform variables
}Shader;




DAPI void bind(const Shader* shader); //Set gpu to use our shaders

DAPI void updateShader(const Shader* shader,const Transform& transform, const Camera& camera);
DAPI Shader* initShader(const std::string& filename);
DAPI std::string loadShader(const std::string& fileName);
DAPI void checkShaderError(GLuint shader, GLuint flag, bool isProgram, const std::string& errorMessage);
DAPI GLuint createShader(const std::string& text, unsigned int type);
DAPI void freeShader(Shader* shader);
