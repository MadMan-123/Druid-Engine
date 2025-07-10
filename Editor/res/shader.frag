#version 410

in vec2 tc;
in vec3 Normal;
in vec3 FragPos;


out vec4 FragColour;



void main()
{	
	vec3 result = vec3(1.0,1.0,1.0);	
	FragColour = vec4(result,1.0);
}
