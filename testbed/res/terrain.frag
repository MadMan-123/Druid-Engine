#version 450 core

in vec2 TexCoord;
in float Height;
in float NoiseScale;
in float NoiseHeight;

uniform sampler2D diffuse;
out vec4 FragColor;
const float uNoiseHeight = 50;
float textureScale = 0.1;
void main() 
{
   

    
    //if the height is above a certain value then apply white
    if (Height / 5 > uNoiseHeight)
    {
        FragColor = vec4(1.0,1.0,1.0,1.0);
        return;
    }
    


    //scale the texture 
    vec2 texCoord = TexCoord * textureScale;
    //get texture 0
    vec3 texColour = texture(diffuse, texCoord).rgb;

    
    
    FragColor = vec4(texColour,1.0);
}
