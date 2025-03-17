#version 410

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 texCoord;
layout (location = 2) in vec3 normal;

out vec2 tc;


uniform mat4 transform;
uniform mat4 model;
out vec3 Normal;
out vec3 FragPos;


void main()
{

	gl_Position = transform * vec4(position*5.0f, 1.0);
	FragPos = vec3(model*vec4(position,1.0));
	Normal = normal;	
	tc = texCoord;	

}
