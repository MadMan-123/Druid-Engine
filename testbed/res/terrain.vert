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
uniform ivec2 terrainSize;      // Width and height of terrain grid
uniform ivec2 terrainBounds;    // Min and max bounds for terrain in world space
uniform int panelIndex;         // Current panel index (0 to N-1)
uniform int panelGridWidth;     // Number of panels per row

void main() {


/*
    // Get height from terrain data
    float height = terrainData[index];

    // Pass data to fragment shader
    Height = height;

    // Create final position with height from terrain data
    vec3 position = vec3(worldX, height, worldZ);

*/


    TexCoord = aTexCoord;
    // Apply transformation matrix
    gl_Position = transform * vec4(aPos, 1.0);
}