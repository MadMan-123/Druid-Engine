//Version number
#version 410
//Layout Qualifer
layout( location = 0 ) out vec4 fragcolor;
//Unfrom variabl
uniform sampler2D albedoTexture;

layout(std140) uniform CoreShaderData {
    vec3 camPos;
    float time;
} CSD;
in vec2 TexCoords;

void main()
{
	vec4 color = texture2D(albedoTexture, TexCoords);
    fragcolor = color;
}