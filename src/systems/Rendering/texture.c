#include "../../../include/druid.h"
#include "../../stb_image.h"

u32 initTexture(const char* fileName)
{
	if (!fileName) {
		ERROR("initTexture: fileName is NULL");
		return 0;
	}

	u32 textureHandler = 0;	
	int width, height, numComponents; //width, height, and no of components of image
	unsigned char* imageData = stbi_load(fileName, &width, &height, &numComponents, 4); //load the image and store the data

	if (imageData == NULL)
	{
		ERROR("texture load failed %s", fileName);
		return 0;
	}
	
	//number of and address of textures
	glGenTextures(1, &textureHandler); 
	
	//bind texture - define type 
	glBindTexture(GL_TEXTURE_2D, textureHandler);

 	//wrap texture outside width
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	//wrap texture outside height
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); 
 	
	//TODO: optional filtering
	//linear filtering for minification (texture is smaller than area)
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // linear filtering for magnifcation (texture is larger)

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData); //Target, Mipmapping Level, Pixel Format, Width, Height, Border Size, Input Format, Data Type of Texture, Image Data

	stbi_image_free(imageData);

	return textureHandler; //return the texture handler
}

u32 createCubeMapTexture(const char** faces,u32 count)
{
	u32 textureHandler;
	glGenTextures(1, &textureHandler);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureHandler);
  	u32 width, height, nChannels;
    for (u32 i = 0; i < count; i++) 
	{
        unsigned char* data = stbi_load(faces[i], &width, &height, &nChannels, 0);
        if (data) 	
		{
			GLenum format = (nChannels == 4) ? GL_RGBA : GL_RGB;
            glTexImage2D(
				GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
				0,
				format,
				width,
				height, 
				0, 
				format, 
				GL_UNSIGNED_BYTE, 
				data);
            stbi_image_free(data);
        } 
		else 
		{
			//TODO: add proper error handling
			printf("Cubemap texture failed to load at path: %s\n", faces[i]);
	
    		stbi_image_free(data);
    		glDeleteTextures(1, &textureHandler);
    		return 0;
        }
    }	
	//setup filter and wrap mode
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	return textureHandler;
}

void freeTexture(u32 texture)
{
	//free the texture from memory
	glDeleteTextures(1, &texture); 
}

void bindTexture(u32 texture,unsigned int unit, GLenum type)
{
	//check if the unit is valid
	assert(unit >= 0 && unit <= 31); 

	//bind the texture to the unit
	glActiveTexture(GL_TEXTURE0 + unit); 
	glBindTexture(type, texture); 
}
