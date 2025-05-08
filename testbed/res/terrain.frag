#version 450 core

in vec2 TexCoord;
in float Height;
in float NoiseScale;
in float NoiseHeight;

uniform sampler2D diffuse;
out vec4 FragColor;

uniform float uNoiseHeight;
float textureScale = 1;
void main() 
{
    
    


    //scale the texture 
    vec2 texCoord = TexCoord * textureScale;
    //get texture 0
    vec3 texColour = texture(diffuse, texCoord).rgb;

    
    
    FragColor = vec4(texColour,1.0);
}
