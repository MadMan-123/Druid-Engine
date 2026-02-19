#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 2) in vec3 aNormal;
layout (location = 1) in vec2 TextCoords;

out Vertex_DATA{
    vec2 tC;
    vec3 Normal;
    vec3 Position;

} vs_Out;

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

uniform mat4 model;

void main()
{
    vs_Out.Normal = mat3(transpose(inverse(model))) * aNormal;
    vs_Out.Position = vec3(model * vec4(aPos, 1.0));
    vs_Out.tC = TextCoords;
    gl_Position = projection * view * model * vec4(aPos, 1.0);

}  
