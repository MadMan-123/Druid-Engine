#include "Shader.h"
#include <iostream>
#include <fstream>



Shader* initShader(const std::string& filename)
{
	//create a new shader
	Shader* shader = (Shader*)malloc(sizeof(Shader));
	//assert that the shader was created
	assert(shader != NULL && "Shader could not be created");
	//initialize the shader

	shader->program = glCreateProgram(); // create shader program (openGL saves as ref number)
	shader->shaders[0] = createShader(loadShader("..\\res\\shader.vert"), GL_VERTEX_SHADER); // create vertex shader
	shader->shaders[1] = createShader(loadShader("..\\res\\shader.frag"), GL_FRAGMENT_SHADER); // create fragment shader

	for (unsigned int i = 0; i < Shader::NUM_SHADERS; i++)
	{
		glAttachShader(shader->program, shader->shaders[i]); //add all our shaders to the shader program "shaders" 
	}

	glBindAttribLocation(shader->program, 0, "position"); // associate attribute variable with our shader program attribute (in this case attribute vec3 position;)
	glBindAttribLocation(shader->program, 1, "texCoord");

	glLinkProgram(shader->program); //create executables that will run on the GPU shaders
	checkShaderError(
		shader->program,
		GL_LINK_STATUS,
		true,
		"Error: Shader program linking failed"); // cheack for error

	glValidateProgram(shader->program); //check the entire program is valid
	checkShaderError(
		shader->program,
		GL_VALIDATE_STATUS,
		true,
		"Error: Shader program not valid");

	shader->uniforms[Shader::TRANSFORM_U] = glGetUniformLocation(shader->program, "transform"); // associate with the location of uniform variable within a program

	return shader;
}

void freeShader(Shader *shader)
{
	for (unsigned int i = 0; i < Shader::NUM_UNIFORMS; i++)
	{
		glDetachShader(shader->program, shader->shaders[i]); //detach shader from program
		
		glDeleteShader(shader->shaders[i]); //delete the sahders
	}
	glDeleteProgram(shader->program); // delete the program

	free(shader); //free the shader
}



void bind(const Shader* shader)
{
	glUseProgram(shader->program); //installs the program object specified by program as part of rendering state
}

void updateShader(const Shader* shader, const Transform& transform, const Camera& camera)
{
	auto mvp = getViewProjection(&camera) * getModel(&transform);
	glUniformMatrix4fv(
		shader->uniforms[Shader::TRANSFORM_U],
	1,
	GLU_FALSE,
	&mvp[0][0]);
}


GLuint createShader( const std::string& text, unsigned int type)
{
	GLuint shader = glCreateShader(type); //create shader based on specified type

	if (shader == 0) //if == 0 shader no created
		std::cerr << "Error type creation failed " << type << '\n';

	const GLchar* stringSource[1]; //convert strings into list of c-strings
	stringSource[0] = text.c_str();
	GLint lengths[1];
	lengths[0] = text.length();

	glShaderSource(shader, 1, stringSource, lengths); //send source code to opengl
	glCompileShader(shader); //get open gl to compile shader code

	checkShaderError(
		shader,
		GL_COMPILE_STATUS,
		false,
		"Error compiling shader!"
		); //check for compile error


	
	return shader;
}

std::string loadShader(const std::string& fileName)
{
	std::ifstream file;
	file.open((fileName).c_str());

	std::string output;
	std::string line;

	if (file.is_open())
	{
		while (file.good())
		{
			getline(file, line);
			output.append(line + "\n");
		}
	}
	else
	{
		std::cerr << "Unable to load shader: " << fileName << std::endl;
	}

	return output;
}

void checkShaderError(GLuint shader, GLuint flag, bool isProgram, const std::string& errorMessage)
{
	GLint success = 0;
	GLchar error[1024] = { 0 };

	if (isProgram)
		glGetProgramiv(shader, flag, &success);
	else
		glGetShaderiv(shader, flag, &success);

	if (success == GL_FALSE)
	{
		if (isProgram)
			glGetProgramInfoLog(shader, sizeof(error), NULL, error);
		else
			glGetShaderInfoLog(shader, sizeof(error), NULL, error);

		std::cerr << errorMessage << ": '" << error << "'" << std::endl;
	}
}

