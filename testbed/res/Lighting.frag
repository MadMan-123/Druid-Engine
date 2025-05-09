#version 450 core
layout(std140, binding = 0) uniform Lighting {
    vec3 lightPos;
    vec3 lightColor;
    vec3 viewPos;
};

in vec2 TexCoord;
in vec3 WorldPos;
in vec3 WorldNormal;

uniform sampler2D diffuseTexture;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(WorldNormal);
    vec3 lightDir = normalize(lightPos - WorldPos);

    // Phong lighting calculations
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    vec3 ambient = 0.1 * lightColor;

    vec3 viewDir = normalize(viewPos - WorldPos);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = 0.5 * spec * lightColor;

    vec4 texColor = texture(diffuseTexture, TexCoord);
    FragColor = vec4((ambient + diffuse + specular) * texColor.rgb, texColor.a);
}