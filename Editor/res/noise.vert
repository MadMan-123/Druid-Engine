#version 420

layout (location = 0) in vec3 VertexPosition;
layout (location = 1) in vec2 TextC;

out vec2 vUv;
out vec3 vN;
out vec4 v_pos;

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

uniform mat4 model;

void main()
{
	v_pos = projection * view * model * vec4(VertexPosition, 1.0);
	vUv = TextC;
	gl_Position = projection * view * model * vec4(VertexPosition, 1.0);
}