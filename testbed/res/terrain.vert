#version 330 core
// Vertex shader inputs
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

// Outputs to fragment shader
out vec2 TexCoord;
out float Height;
out float NoiseScale;
out float NoiseHeight;


// Uniform variables for noise control
float uNoiseScale = 0.5;
float uNoiseHeight = 100;


uniform float uTime;
uniform mat4 transform;




// Pseudo-random hash function
float hash(vec2 p) {
    p  = 50.0*fract( p*0.3183099 + vec2(0.71,0.113));
    return -1.0+2.0*fract( p.x*p.y*(p.x+p.y) );
}

// Smooth interpolation
float smoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
	
    // Smooth Interpolation Curve
    vec2 u = f*f*(3.0-2.0*f);

    return mix( mix( hash( i + vec2(0.0,0.0) ), 
                     hash( i + vec2(1.0,0.0) ), u.x),
                mix( hash( i + vec2(0.0,1.0) ), 
                     hash( i + vec2(1.0,1.0) ), u.x), u.y);
}

// Fractal Brownian Motion (fBm) for more natural noise
float perlinNoise(vec2 p) {
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
    // Create a copy of the original position
    vec3 pos = aPos;
    
    // Generate noise using texture coordinates
    float noise = perlinNoise(aTexCoord * uNoiseScale + uTime * 0.1);
    
    // Apply noise to vertex height
    pos.y += noise * uNoiseHeight;
    
    // Pass height to fragment shader for potential coloring
    Height = pos.y;
    
    // Standard MVP transformation (you'll need to add model, view, projection matrices)
    gl_Position = transform* vec4(pos, 1.0);
    
    // Pass through texture coordinates
    TexCoord = aTexCoord;
}
