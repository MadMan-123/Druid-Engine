#version 410

layout (location = 0) in vec3 position;


uniform mat4 transform;
uniform mat4 model;
//out vec3 Normal;
//out vec3 FragPos;


void main()
{
	//FragPos = vec3(model*vec4(position,1.0));

	gl_Position = transform * vec4(position, 1.0);

}
