#version 450 core

in vec2 TexCoord;

uniform sampler2D diffuse;
out vec4 FragColor;


in vec3 WorldNormal;
in vec3 WorldPos;

void main()
{
    vec2 texCoord = TexCoord * 2;
   
    texCoord.x += 0.5;
    texCoord.y += 0.5;

    //get the texture 
    vec3 texColour = texture(diffuse, texCoord ).rgb;

    FragColor = vec4(texColour ,1.0); 
}
