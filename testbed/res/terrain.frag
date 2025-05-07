#version 450 core

in vec2 TexCoord;
in float Height;
in float NoiseScale;
in float NoiseHeight;

uniform sampler2D diffuse;
out vec4 FragColor;

uniform float uNoiseHeight;
float textureScale = 10;
void main() 
{
    
    
    // Color based on height
    vec3 lowColor = vec3(0.1, 0.5, 0.1);     // Green for low areas
    vec3 highColor = vec3(0.8, 0.8, 0.8);    // White/gray for high areas

    //scale the texture 
    vec2 texCoord = TexCoord * textureScale;
    //get texture 0
    vec3 texColour = texture(diffuse, texCoord).rgb;

    
    
    FragColor = vec4(texColour,1.0);
}
