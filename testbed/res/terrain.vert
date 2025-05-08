#version 450 core

// Vertex shader inputs
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

// Terrain data buffer (1D array treated as 2D)
layout (std430, binding = 0) buffer TerrainData {
    float terrainData[];
};

// Outputs to fragment shader
out vec2 TexCoord;
out float Height;

// Uniforms
uniform mat4 transform;
uniform float uTime;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = transform * vec4(aPos, 1.0);
    Height = aPos.y;
}