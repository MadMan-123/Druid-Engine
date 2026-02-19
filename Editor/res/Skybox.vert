#version 450
layout (location = 0) in vec3 aPos;

out vec3 TexCoord;


//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

void main() 
{

    TexCoord = aPos;
    // Remove translation from view matrix for skybox
    mat4 skyboxView = mat4(mat3(view));
    vec4 pos = projection * skyboxView * vec4(aPos, 1.0);
    gl_Position = pos.xyww; 
    
}       