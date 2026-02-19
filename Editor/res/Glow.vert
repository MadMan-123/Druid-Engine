#version 420

layout (location = 0) in vec3 VertexPosition;
layout (location = 2) in vec3 VertexNormal;

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

uniform mat4 model;

out vec3 vN;


void main()
{
	vN = VertexNormal; //static

	//vN = mat3(transpose(inverse(view*model))) * VertexNormal;

	gl_Position = projection * view * model * vec4(VertexPosition, 1.0);
}