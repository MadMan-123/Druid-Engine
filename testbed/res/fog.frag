#version 410

out vec4 FragColour;

layout (location = 0) in vec2 texCoord;

in VertexData
{
	vec4 position;
	vec3 worldPos;
}VertexDataIn;

vec3 fogColor = vec3(0.01,0.01,0.01);

float maxDist = 25.0;
float minDist = 0.0;

uniform sampler2D diffuse;

uniform vec3 camPos;


void main()
{
	float camDist = distance(camPos , VertexDataIn.worldPos);

	float fogFactor = (maxDist - camDist)/(maxDist - minDist);

	fogFactor = clamp(fogFactor,0.0,1.0);
	
	vec3 lightColour = vec3(0.4,0.4,0.4);

	vec3 colour = mix(fogColor,lightColour,fogFactor);
	
	FragColour = vec4(colour,1.0);
}
