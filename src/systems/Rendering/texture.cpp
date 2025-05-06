#include "../../../include/druid.h"
#include "../../stb_image.h"
#include <iostream>

u32 initTexture(const std::string& fileName)
{
	u32 textureHandler;	
	int width, height, numComponents; //width, height, and no of components of image
	unsigned char* imageData = stbi_load((fileName).c_str(), &width, &height, &numComponents, 4); //load the image and store the data

	if (imageData == NULL)
	{
		std::cerr << "texture load failed" << fileName << std::endl;
	}
	
	// number of and address of textures
	glGenTextures(1, textureHandler); 
	
	//bind texture - define type 
	glBindTexture(GL_TEXTURE_2D, textureHandler);

 	// wrap texture outside width
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	// wrap texture outside height
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); 
 	
	// linear filtering for minification (texture is smaller than area)
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // linear filtering for magnifcation (texture is larger)

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData); //Target, Mipmapping Level, Pixel Format, Width, Height, Border Size, Input Format, Data Type of Texture, Image Data

	stbi_image_free(imageData);

	return textureHandler; //return the texture handler
}

void freeTexture(u32 texture)
{
	glDeleteTextures(1, texture); // number of and address of textures
}

void bindTexture(u32 texture,unsigned int unit)
{
	assert(unit >= 0 && unit <= 31); /// check we are working with one of the 32 textures

	glActiveTexture(GL_TEXTURE0 + unit); //set acitve texture unit
	glBindTexture(GL_TEXTURE_2D, texture); //type of and texture to bind to unit
}
