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

//lighting


out vec4 FragColour;



void main()
{		
	vec3 diffuse = texture2D(albedoTexture,tc).rgb; 

	



	FragColour = vec4(diffuse,transparency);

}
