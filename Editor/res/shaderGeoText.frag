//Version number
#version 410
//Layout Qualifer
layout( location = 0 ) out vec4 fragcolor;
//Unfrom variabl
uniform sampler2D albedoTexture;

in vec2 TexCoords;

void main()
{
//Setting each vector component to uniform varaible then setting final colour
	vec4 color = texture2D(albedoTexture, TexCoords);
    fragcolor = color;
}