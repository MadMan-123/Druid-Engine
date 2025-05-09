#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;

layout(std140, binding = 0) uniform Lighting {
    vec3 lightPos;
    vec3 lightColor;
    vec3 viewPos;
};

uniform mat4 transform; // MVP matrix
uniform mat4 model;    // Model matrix only

out vec2 TexCoord;
out vec3 WorldPos;
out vec3 WorldNormal;

void main()
{
    gl_Position = transform * vec4(aPos, 1.0);
    WorldPos = vec3(model * vec4(aPos, 1.0));
    WorldNormal = mat3(model) * aNormal;
    TexCoord = aTexCoord;
}