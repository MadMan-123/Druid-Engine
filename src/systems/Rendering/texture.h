#pragma once
#include <string>
#include <GL\glew.h>
#include "../../defines.h"

typedef struct{
	GLuint handler;
}Texture;

DAPI void bind(Texture* texture,unsigned int unit); // bind upto 32 textures
DAPI void initTexture(Texture* texture,const std::string& fileName);
DAPI void freeTexture(Texture* texture); //free the texture from memory

