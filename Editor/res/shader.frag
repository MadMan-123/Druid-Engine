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
uniform vec3 colour;
uniform float transparency;

out vec4 FragColour;



void main()
{		
	vec3 diffuse = texture2D(albedoTexture,tc).rgb;

	vec3 combine = colour;


	FragColour = vec4(combine,transparency);

}
