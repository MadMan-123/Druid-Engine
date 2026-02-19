#version 420 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2 TexCoords;

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};


vec4 shakeCamera(vec2 aPos)
{
    vec4 pos = vec4(aPos, 0.0, 1.0);
    float strength = 0.01;
    pos.x += cos(time * 10) * strength;        
    pos.y += sin(time * 15) * strength; 
    return pos;
}

void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    //gl_Position = shakeCamera(aPos);
    TexCoords = aTexCoords;
}  