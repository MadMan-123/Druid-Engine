#version 400

uniform vec3 lightDir;
in vec3 normal;
in vec2 tC;
out vec4 FragColour;

void main()
{
	float intensity;
	vec4 colour;

	intensity = dot(lightDir, normal);

	if (intensity > 0.75)
		colour = vec4(1.0,0.5,0.5,1.0);
	else if (intensity > 0.5)
		colour = vec4(0.6,0.3,0.3,1.0);
	else if	(intensity > 0.25)
		colour = vec4(0.4,0.2,0.2,1.0);
	else
		colour = vec4(0.2,0.1,0.1,1.0);
	
	FragColour = colour;
}