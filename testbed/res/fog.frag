#version 410

out vec4 FragColour;

layout (location = 0) in vec2 texCoord;

in vec4 v_pos;
in vec3 current;
vec3 fogColor = vec3(0,0,0);

float maxDist = 20.0f;
float minDist = 0.0f;

uniform sampler2D diffuse;

uniform vec3 camPos;

void main()
{

	float camDist = distance(camPos , current);	
	float fogFactor = (maxDist - camDist)/(maxDist - minDist);

	fogFactor = clamp(fogFactor,0.0,1.0);
	
	vec3 lightColour = vec3(0.3,0.3,0.3);

	vec3 colour = mix(fogColor,lightColour,fogFactor);
	
	

	FragColour = vec4(colour,1.0);

}
