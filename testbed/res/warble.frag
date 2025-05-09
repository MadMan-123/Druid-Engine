#version 450 core
in vec2 TexCoord;
uniform sampler2D diffuse;
out vec4 FragColor;
void main() 
{
    //get the texture
    vec3 texColour = texture(diffuse, TexCoord).rgb;
    FragColor = vec4(texColour, 1.0);
}
