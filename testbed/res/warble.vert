#version 450 core

//position
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;

uniform mat4 transform;
// Core shader data (time + viewProj) provided via UBO
layout(std140) uniform CoreShaderData {
    vec4 time;
    mat4 viewProj;
} CSD;

// keep existing code compatible
#define time CSD.time.x
const float warbleFreq = 0.01; // Frequency of the warble effect
const float warbleAmp= 0.5;  // Amplitude of the warble effect

out vec2 TexCoord;

float hash(vec2 p)
{
    p  = 50.0*fract( p*0.3183099 + vec2(0.71,0.113));
    return -1.0+2.0*fract( p.x*p.y*(p.x+p.y) );
}

// Smooth interpolation
float smoothNoise(vec2 p) 
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Smooth Interpolation Curve
    vec2 u = f*f*(3.0-2.0*f);

    return mix( mix( hash( i + vec2(0.0,0.0) ),
    hash( i + vec2(1.0,0.0) ), u.x),
    mix( hash( i + vec2(0.0,1.0) ),
    hash( i + vec2(1.0,1.0) ), u.x), u.y);
}

float noise(vec2 p)
{
    float f = 0.0;
    float warbleAmp = 0.5;
    float warbleFreq = 1.0;

    // Octaves for more detailed noise
    for (int i = 0; i < 6; i++) 
    {
        f += warbleAmp * smoothNoise(warbleFreq * p);
        warbleAmp *= 0.5;
        warbleFreq *= 2.0;
    }

    return f;
}
void main()
{
    vec2 noiseInput = aPos.xz * warbleFreq + time * 0.5;
    float noiseValue = noise(noiseInput);

    vec3 warbledPosition = aPos + aNormal * noiseValue * warbleAmp;

    TexCoord = aTexCoord;
    
    gl_Position = transform * vec4(warbledPosition, 1.0);
}
