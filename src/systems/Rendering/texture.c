#include "../../../include/druid.h"
#include "../../external/stb_image.h"

u32 initTexture(const c8* fileName)
{
	if (!fileName) 
	{
		ERROR("initTexture: fileName is NULL");
		return 0;
	}

	u32 textureHandler = 0;	
	i32 width, height, numComponents;
	u8 * imageData = stbi_load(fileName, &width, &height, &numComponents, 4); //load the image and store the data

	if (imageData == NULL)
	{

		ERROR("STB ERROR %s", stbi_failure_reason());
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

		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // linear filtering for magnifcation (texture is larger)
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // linear filtering for minification (texture is smaller)
 // linear filtering for magnifcation (texture is larger)



		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData); //Target, Mipmapping Level, Pixel Format, Width, Height, Border Size, Input Format, Data Type of Texture, Image Data
	glGenerateMipmap(GL_TEXTURE_2D); // Generate mipmaps
 //Target, Mipmapping Level, Pixel Format, Width, Height, Border Size, Input Format, Data Type of Texture, Image Data

	stbi_image_free(imageData);

	return textureHandler;
}

u32 createCubeMapTexture(const c8** faces,u32 count)
{
	u32 textureHandler;
	glGenTextures(1, &textureHandler);
	glBindTexture(GL_TEXTURE_CUBE_MAP, textureHandler);
  	i32 width, height, nChannels;
    for (u32 i = 0; i < count; i++) 
	{
        u8 * data = stbi_load(faces[i], &width, &height, &nChannels, 0);
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
			ERROR("Cubemap face %u failed to load: %s", i, faces[i]);
			ERROR("STB reason: %s", stbi_failure_reason());

    		stbi_image_free(data);
    		glDeleteTextures(1, &textureHandler);
    		return 0;
        }
    }	
	//setup filter and wrap mode
	// Use trilinear filtering (mipmaps) for smoother transitions between LODs
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	// Generate mipmaps for the cubemap. This helps with filtering and reduces
	// visible seams/artifacts when sampling with varying roughness or LOD.
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	return textureHandler;
}

void freeTexture(u32 texture)
{
	//free the texture from memory
	glDeleteTextures(1, &texture); 
}

void bindTexture(u32 texture,u32 unit, GLenum type)
{
	assert(unit >= 0 && unit <= 31);
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(type, texture);
	profileCountTextureBind();
}
