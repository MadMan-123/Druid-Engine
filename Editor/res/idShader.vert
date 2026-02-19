#version 420

layout (location = 0) in vec3 position;

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

uniform mat4 model;
//out vec3 Normal;
//out vec3 FragPos;


void main()
{
	//FragPos = vec3(model*vec4(position,1.0));

	gl_Position = projection * view * model * vec4(position, 1.0);

}
