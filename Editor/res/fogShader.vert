#version 400

layout (location = 0) in vec3 VertexPosition;
layout (location = 1) in vec3 VertexNormal;

uniform mat4 transform;

out vec3 v_norm;
out vec4 v_pos; 

void main()
{
	v_norm = VertexNormal;
	v_pos = transform * vec4(VertexPosition, 1.0);
	gl_Position = transform * vec4(VertexPosition, 1.0);
}