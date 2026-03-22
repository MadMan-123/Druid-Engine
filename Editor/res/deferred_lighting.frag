#version 420

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;

layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

void main()
{
    // Retrieve data from GBuffer
    vec3 FragPos   = texture(gPosition, TexCoords).rgb;
    vec3 Normal    = normalize(texture(gNormal, TexCoords).rgb);
    vec3 Albedo    = texture(gAlbedoSpec, TexCoords).rgb;
    float Specular = texture(gAlbedoSpec, TexCoords).a;

    // Simple directional light for now
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 lightColor = vec3(1.0, 0.98, 0.95);

    // Ambient
    vec3 ambient = 0.15 * Albedo;

    // Diffuse
    float diff = max(dot(Normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor * Albedo;

    // Specular (Blinn-Phong)
    vec3 viewDir = normalize(camPos - FragPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(Normal, halfDir), 0.0), 32.0) * Specular;
    vec3 specular = spec * lightColor;

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}
