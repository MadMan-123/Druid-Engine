#version 400

layout (location = 0) in vec3 VertexPosition;
layout (location = 1) in vec2 TextC;

out vec2 vUv;
out vec3 vN;
out vec4 v_pos; 
uniform mat4 transform;

void main()
{
	v_pos = transform * vec4(VertexPosition, 1.0);
	vUv = TextC;
	gl_Position = transform * vec4(VertexPosition, 1.0);
}