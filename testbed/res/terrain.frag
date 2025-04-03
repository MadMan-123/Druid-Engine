#version 330 core

in vec2 TexCoord;
in float Height;
in float NoiseScale;
in float NoiseHeight;

out vec4 FragColor;

uniform float uNoiseHeight;

void main() 
{
    // Color based on height
    vec3 lowColor = vec3(0.1, 0.5, 0.1);     // Green for low areas
    vec3 highColor = vec3(0.8, 0.8, 0.8);    // White/gray for high areas
    


    // Normalize height to 0-1 range
    float normalizedHeight = clamp(Height / NoiseHeight, 0.0, 1.0);
    
    // Interpolate color based on height
    vec3 terrainColor = mix(lowColor, highColor, normalizedHeight);
    
    FragColor = vec4(terrainColor, 1.0);
}
