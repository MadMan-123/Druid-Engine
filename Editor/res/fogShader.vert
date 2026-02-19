#version 420

layout (location = 0) in vec3 VertexPosition;
layout (location = 1) in vec3 VertexNormal;

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

uniform mat4 model;

out vec3 v_norm;
out vec4 v_pos; 

void main()
{
	v_norm = VertexNormal;
	v_pos = projection * view * model * vec4(VertexPosition, 1.0);
	gl_Position = projection * view * model * vec4(VertexPosition, 1.0);
}