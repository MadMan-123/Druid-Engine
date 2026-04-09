#version 420

out vec4 FragColor;
in vec2 TexCoords;

// GBuffer textures
uniform sampler2D gPosition;   // rgb = world pos, a = metallic
uniform sampler2D gNormal;      // rgb = normal, a = roughness
uniform sampler2D gAlbedoSpec;  // rgb = albedo, a = emissive

// Environment cubemap for IBL reflections
uniform samplerCube envMap;

layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

uniform vec3 u_sunPos;
uniform float u_sunRadius;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec4 posMetal    = texture(gPosition, TexCoords);
    vec4 normRough   = texture(gNormal, TexCoords);
    vec4 albedoEmiss = texture(gAlbedoSpec, TexCoords);

    vec3  FragPos      = posMetal.rgb;
    float metallicVal  = posMetal.a;
    vec3  N            = normalize(normRough.rgb);
    float roughnessVal = normRough.a;
    vec3  albedo       = albedoEmiss.rgb;
    float emissiveVal  = albedoEmiss.a;

    // Skip empty pixels
    if (normRough.rgb == vec3(0.0))
        discard;

    // Emissive objects bypass lighting — output bright color directly
    if (emissiveVal > 0.0)
    {
        vec3 emissiveColor = albedo * (1.0 + emissiveVal * 4.0);
        // Tone map
        emissiveColor = emissiveColor / (emissiveColor + vec3(1.0));
        emissiveColor = pow(emissiveColor, vec3(1.0 / 2.2));
        FragColor = vec4(emissiveColor, 1.0);
        return;
    }

    vec3 V = normalize(camPos - FragPos);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallicVal);

    vec3 Lo = vec3(0.0);

    // Directional fill light
    {
        vec3 L = normalize(vec3(0.5, 1.0, 0.3));
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        vec3 lightColor = vec3(1.0, 0.98, 0.95) * 0.5;

        float NDF = distributionGGX(N, H, roughnessVal);
        float G   = geometrySmith(N, V, L, roughnessVal);
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = NDF * G * F / max(4.0 * NdotV * NdotL + 0.0001, 0.0001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallicVal);
        Lo += (kD * albedo / PI + specular) * lightColor * NdotL;
    }

    // Sun point light
    {
        vec3 toSun = u_sunPos - FragPos;
        float dist = length(toSun);
        vec3 L = normalize(toSun);
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        float attenuation = 1.0 / (1.0 + 0.001 * dist * dist);
        vec3 lightColor = vec3(1.0, 0.9, 0.7) * 80.0 * attenuation;

        float NDF = distributionGGX(N, H, roughnessVal);
        float G   = geometrySmith(N, V, L, roughnessVal);
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = NDF * G * F / max(4.0 * NdotV * NdotL + 0.0001, 0.0001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallicVal);
        Lo += (kD * albedo / PI + specular) * lightColor * NdotL;
    }

    // Environment mapping (IBL)
    vec3 R = reflect(-V, N);
    float lod = roughnessVal * 4.0;
    vec3 envColor = textureLod(envMap, R, lod).rgb;

    vec3 F_env = fresnelSchlickRoughness(NdotV, F0, roughnessVal);
    vec3 kD_env = (vec3(1.0) - F_env) * (1.0 - metallicVal);

    vec3 irradiance = textureLod(envMap, N, 4.0).rgb;
    vec3 diffuseIBL = irradiance * albedo * kD_env;
    vec3 specularIBL = envColor * F_env * metallicVal;

    vec3 ambient = diffuseIBL + specularIBL;

    vec3 result = ambient + Lo;

    // Tone mapping (Reinhard)
    result = result / (result + vec3(1.0));

    // Gamma correction
    result = pow(result, vec3(1.0 / 2.2));

    FragColor = vec4(result, 1.0);
}
