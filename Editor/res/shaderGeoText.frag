//Version number
#version 420
//Layout Qualifer
layout( location = 0 ) out vec4 fragcolor;
//Unfrom variabl
uniform sampler2D albedoTexture;

layout (std140, binding = 0) uniform CoreShaderData {
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};
in vec2 TexCoords;

void main()
{
	vec4 color = texture2D(albedoTexture, TexCoords);
    fragcolor = color;
}