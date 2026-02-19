#version 420

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D screenTexture;
uniform float strength; // for shake
uniform vec2 texelSize; // 1.0/textureSize

vec3 quantize(vec3 color, int steps)
{
    //floor the colour 
    color.rgb = floor(color.rgb * float(steps)) / float(steps);
    return color;
}

vec3 invert(vec3 color)
{
    return vec3(1.0) - color;
}

vec3 grayscale(vec3 color)
{
    float l = 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
    return vec3(l);
}

vec3 shake(vec2 uv)
{
    vec2 offset = vec2(sin(time * 10.0), cos(time * 7.0)) * strength;
    return texture(screenTexture, uv + offset).rgb;
}

vec3 kernel(vec2 uv)
{
    float offx = texelSize.x;
    float offy = texelSize.y;
    vec2 offsets[9] = vec2[](
        vec2(-offx,  offy), vec2(0.0,  offy), vec2(offx,  offy),
        vec2(-offx,  0.0),  vec2(0.0,  0.0),  vec2(offx,  0.0),
        vec2(-offx, -offy), vec2(0.0, -offy), vec2(offx, -offy)
    );
    float kernel[9] = float[](
        -1, -1, -1,
        -1,  9, -1,
        -1, -1, -1
    );
    vec3 sampleTex[9];
    for (int i = 0; i < 9; ++i)
        sampleTex[i] = texture(screenTexture, uv + offsets[i]).rgb;
    vec3 accum = vec3(0.0);
    for (int i = 0; i < 9; ++i)
        accum += sampleTex[i] * kernel[i];
    return accum;
}

void main()
{
    vec3 base = texture(screenTexture, TexCoords).rgb;
    vec3 outc = quantize(base,2);



    FragColor = vec4(outc, 1.0);
}
