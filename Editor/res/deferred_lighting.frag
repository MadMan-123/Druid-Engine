#version 430

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform samplerCube envMap;

layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

struct Light
{
    float posX, posY, posZ;
    float range;
    float colorR, colorG, colorB;
    float intensity;
    float dirX, dirY, dirZ;
    float innerCone;
    float outerCone;
    uint  type;
    float _pad0, _pad1;
};

layout (std430, binding = 3) buffer LightBuffer
{
    uint  lightCount;
    uint  _pad[3];
    Light lights[];
};

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

vec3 evalLight(Light l, vec3 fragPos, vec3 N, vec3 V, float NdotV, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 lightPos = vec3(l.posX, l.posY, l.posZ);
    vec3 lightCol = vec3(l.colorR, l.colorG, l.colorB) * l.intensity;
    vec3 lightDir = vec3(l.dirX, l.dirY, l.dirZ);

    vec3 L;
    float atten = 1.0;

    if (l.type == 1u) // directional
    {
        L = normalize(-lightDir);
    }
    else // point or spot
    {
        vec3 toLight = lightPos - fragPos;
        float dist = length(toLight);
        L = toLight / max(dist, 0.0001);

        float falloff = clamp(1.0 - (dist / max(l.range, 0.001)), 0.0, 1.0);
        atten = falloff * falloff;

        if (l.type == 2u) // spot
        {
            float theta = dot(L, normalize(-lightDir));
            float epsilon = l.innerCone - l.outerCone;
            float spotAtten = clamp((theta - l.outerCone) / max(epsilon, 0.0001), 0.0, 1.0);
            atten *= spotAtten;
        }
    }

    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = NDF * G * F / max(4.0 * NdotV * NdotL + 0.0001, 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * lightCol * NdotL * atten;
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

    if (normRough.rgb == vec3(0.0))
        discard;

    if (emissiveVal > 0.0)
    {
        vec3 emissiveColor = albedo * (1.0 + emissiveVal * 4.0);
        emissiveColor = emissiveColor / (emissiveColor + vec3(1.0));
        emissiveColor = pow(emissiveColor, vec3(1.0 / 2.2));
        FragColor = vec4(emissiveColor, 1.0);
        return;
    }

    vec3 V = normalize(camPos - FragPos);
    float NdotV = max(dot(N, V), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallicVal);

    vec3 Lo = vec3(0.0);

    // Accumulate all lights from the SSBO
    for (uint i = 0u; i < lightCount; i++)
    {
        Lo += evalLight(lights[i], FragPos, N, V, NdotV, albedo, metallicVal, roughnessVal, F0);
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

    // Fallback: no lights = unlit albedo so the scene isn't black
    if (lightCount == 0u)
        result = albedo;

    // Tone mapping (Reinhard)
    result = result / (result + vec3(1.0));

    // Gamma correction
    result = pow(result, vec3(1.0 / 2.2));

    FragColor = vec4(result, 1.0);
}
