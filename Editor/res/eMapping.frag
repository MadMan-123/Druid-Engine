#version 420 core

in Vertex_DATA
{
    vec2 tC;
    vec3 Normal;
    vec3 Position;
} vs_Out;

out vec4 FragColor;
layout(std140) uniform CoreShaderData {
    vec3 camPos;
    float time;
} CSD;

uniform samplerCube skybox;
layout(binding = 2) uniform sampler2D albedoTexture;

float mixFactor = 0.7;
void main()
{
    vec3 I = normalize(vs_Out.Position - CSD.camPos);
    vec3 R = reflect(I, normalize(vs_Out.Normal));

    vec4 colour = vec4(texture(skybox, R).rgb, 1.0);
    vec4 baseColor = texture(albedoTexture, vs_Out.tC);
    vec4 final = vec4(mix(baseColor.rgb, colour.rgb, mixFactor), 1.0);

    FragColor = final;
}

