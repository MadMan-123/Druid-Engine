#version 410



out vec4 FragColour;

uniform vec3 entityID;

void main()
{		
	FragColour = vec4(entityID,1.0);
}
