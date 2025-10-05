#version 410

in vec2 tc;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D albedoTexture;
uniform sampler2D metallicTexture;
uniform sampler2D roughnessTexture;
uniform sampler2D normalTexture;
uniform float roughness;
uniform float metallic;
uniform float transparency;
uniform vec3 colour;
//lighting


out vec4 FragColour;



void main()
{		
	vec3 diffuse = texture(albedoTexture,tc).rgb; 
	vec3 metallicColor = texture(metallicTexture, tc).rgb;
	vec3 roughnessColor = texture(roughnessTexture, tc).rgb;
	vec3 normalColor = texture(normalTexture, tc).rgb;

	vec3 finalColor;
	//if white dont do anything
	if(!(colour.r == 1.0 && colour.g == 1.0 && colour.b == 1.0))
		finalColor = diffuse * colour;
	else
		finalColor = diffuse;


	FragColour = vec4(finalColor, transparency);

}
