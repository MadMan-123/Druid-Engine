#version 450 core

// Vertex shader inputs
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

// Outputs to fragment shader
out vec2 TexCoord;

out vec3 WorldNormal;
out vec3 WorldPos;
// Uniforms
uniform mat4 transform;
uniform mat4 model;
//water noise generation
uniform float uTime;   
const float speed = 0.5;

// Uniform variables for noise control
float uNoiseScale = 10;
float uNoiseHeight = 100;






// Pseudo-random hash function
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

float perlinNoise(vec2 p)
{
    float f = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    // Octaves for more detailed noise
    for (int i = 0; i < 6; i++) {
        f += amplitude * smoothNoise(frequency * p);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return f;
}

void main()
{
    TexCoord = aTexCoord;
    vec3 pos = aPos;

    float noise = perlinNoise(vec2(pos.x + uTime * 0.5, pos.z + uTime * 0.8));
    pos.y += noise * uNoiseHeight;

    WorldPos = vec3(model * vec4(aPos, 1.0));
    WorldNormal = normalize(mat3(transpose(inverse(model))) * aNormal);
    gl_Position = transform * vec4(pos, 1.0);
}