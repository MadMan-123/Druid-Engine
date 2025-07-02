#version 410

in vec2 tc;
in vec3 Normal;
in vec3 FragPos;

uniform float u_time;
uniform sampler2D diffuse;
uniform vec3 lightPos;

float ambientStrength = 0.1;


out vec4 FragColour;


vec3 lightColour = vec3(1.0,1.0,1.0);

void main()
{	
	vec3 norm = normalize(Normal);
	vec3 lightDir = normalize(lightPos - FragPos);

	float diff = max(dot(norm, lightDir),0.0);
	vec3 mixxed = diff * lightColour;

	vec4 texCol = texture2D(diffuse, tc);
	vec3 ambient = ambientStrength * lightColour; 
	
	vec3 result = (ambient + mixxed) * texCol.rgb;	
	FragColour = vec4(result,1.0);
}
