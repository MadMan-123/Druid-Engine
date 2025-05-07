#version 450 core
// Vertex shader inputs
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (binding = 0) buffer TerrainData {
    vec4 terrainData[];
};

// Outputs to fragment shader
out vec2 TexCoord;
out float Height;
out float NoiseScale;
out float NoiseHeight;

uniform ivec2 terrainSize;

// Uniform variables for noise control
float uNoiseScale = 0.5;
float uNoiseHeight = 100;


uniform float uTime;
uniform mat4 transform;







void main() {
    int x = int(aPos.x);
    int y = int(aPos.y);
    int index = y * terrainSize.x + x;
    float height = terrainData[index].x; // or .z if you packed it differently

    vec3 position = vec3(x, height, y); // XZ plane
    gl_Position =transform * vec4(aPos, 1.0);

    // Pass through texture coordinates
    TexCoord = aTexCoord;
}