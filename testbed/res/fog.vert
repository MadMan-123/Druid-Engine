#version 410

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

uniform mat4 model;
uniform mat4 transform;
out VertexData
{
	vec4 position;
	vec3 worldPos;
} VertexDataOut;




void main()
{
	VertexDataOut.position = transform * vec4(position,1.0);

	gl_Position = VertexDataOut.position;

	vec3 pos = (model * vec4(position,1.0)).xyz;

	VertexDataOut.worldPos = pos;

}
