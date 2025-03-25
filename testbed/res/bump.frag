#version 400

in vec2 tC;

uniform sampler2D diffuse;
uniform sampler2D normal;


out vec4 FragColour;

void main()
{
	vec3 normal = texture2D(normal, tC).rgb;
	normal = normalize(normal * 2.0 - 1.0); 

	vec3 colour = texture2D(diffuse, tC).rgb;
	vec3 ambient = 0.8 * colour;
	vec3 lightDir = vec3(0.5,0.5,0.5);

	float diff = max(dot(lightDir, normal), 0.0);
	vec3 diffuse = diff * colour;

	vec3 viewDir = vec3(0.5,0.5,0.5);
    //vec3 reflectDir = reflect(-lightDir, normal); for more accurate spec when using TBN
    vec3 halfwayDir = normalize(lightDir + viewDir);  

    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
	vec3 specular = vec3(0.2) * spec;
	FragColour = vec4(ambient + diffuse + specular, 1.0);
}
