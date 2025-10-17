
#version 450 core
out vec4 FragColor;

in vec3 TexCoord;

// explicit binding to avoid conflicts with other sampler types
uniform samplerCube skybox;

void main()
{
    FragColor = texture(skybox, TexCoord);
}