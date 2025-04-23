#pragma once
#include <string>
#include <GL\glew.h>
#include "../../Core/transform.h"
#include "camera.h"
#include "../../core/maths.h"
#include "../../defines.h"
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
		MODEL_U,
		NUM_UNIFORMS
	};

	GLuint program; // Track the shader program
	GLuint shaders[NUM_SHADERS]; //array of shaders
	GLuint uniforms[NUM_UNIFORMS]; //no of uniform variables
}Shader;



typedef union{
	u32 u32;
	f32 f32;
	bool b;
	Vec2 v2;
	Vec3 v3;
}ShaderValue;


DAPI void bind(const Shader* shader); //Set gpu to use our shaders


DAPI void updateShader(const Shader* shader, const char* variableName, const ShaderValue value);

DAPI Shader* initShader(const std::string& filename);
DAPI std::string loadShader(const std::string& fileName);
DAPI void checkShaderError(GLuint shader, GLuint flag, bool isProgram, const std::string& errorMessage);
DAPI GLuint createShader(const std::string& text, unsigned int type);
DAPI void freeShader(Shader* shader);
