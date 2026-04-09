#version 420
layout(early_fragment_tests) in;

in vec2 tc;
in vec3 Normal;
in vec3 FragPos;
in mat3 TBN;

// Material textures (bound by updateMaterial)
uniform sampler2D albedoTexture;    // unit 0
uniform sampler2D normalTexture;    // unit 1
uniform sampler2D metallicTexture;  // unit 2
uniform sampler2D roughnessTexture; // unit 3

// Material scalars
uniform float roughness;
uniform float metallic;
uniform float transparency;
uniform vec3  colour;
uniform float emissive;

// GBuffer MRT outputs
layout (location = 0) out vec4 gPosition;   // xyz = world pos, w = metallic
layout (location = 1) out vec4 gNormal;      // xyz = world normal, w = roughness
layout (location = 2) out vec4 gAlbedoSpec;  // rgb = albedo, a = emissive

void main()
{
    // Albedo: texture * colour tint
    vec4 albedoSample = texture(albedoTexture, tc);
    vec3 albedo = albedoSample.rgb;
    if (!(colour.r == 1.0 && colour.g == 1.0 && colour.b == 1.0))
        albedo *= colour;

    // Normal mapping: sample tangent-space normal and transform to world space
    vec3 N = Normal;
    vec3 normalMap = texture(normalTexture, tc).rgb;
    if (normalMap != vec3(0.0))
    {
        normalMap = normalMap * 2.0 - 1.0;
        N = normalize(TBN * normalMap);
    }

    // Metallic: texture overrides scalar when texture has data
    float metallicVal = metallic;
    vec3 metallicSample = texture(metallicTexture, tc).rgb;
    if (metallicSample.r > 0.001 || metallicSample.g > 0.001)
        metallicVal = metallicSample.r;

    // Roughness: texture overrides scalar when texture has data
    float roughnessVal = roughness;
    vec3 roughnessSample = texture(roughnessTexture, tc).rgb;
    if (roughnessSample.r > 0.001 || roughnessSample.g > 0.001)
        roughnessVal = roughnessSample.r;

    // Write to GBuffer
    gPosition  = vec4(FragPos, metallicVal);
    gNormal    = vec4(N, roughnessVal);
    gAlbedoSpec = vec4(albedo, emissive);
}
