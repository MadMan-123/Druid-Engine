#version 450 core

// Vertex shader inputs
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;

uniform vec3 lightPos;  
uniform vec3 lightColor;
uniform float lightIntensity;
uniform vec3 ambientColor;// Terrain data buffer (1D array treated as 2D)
layout (std430, binding = 0) buffer TerrainData {
    float terrainData[];
};

// Outputs to fragment shader
out vec2 TexCoord;
out float Height;

// Uniforms
uniform mat4 transform;
uniform mat4 model;    
uniform float uTime;
out vec3 WorldPos;
out vec3 WorldNormal;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = transform * vec4(aPos, 1.0);
    WorldPos = vec3(model * vec4(aPos, 1.0));
    WorldNormal = normalize(mat3(transpose(inverse(model))) * aNormal);
    Height = aPos.y;
}