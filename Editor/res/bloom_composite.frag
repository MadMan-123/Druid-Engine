#version 420

out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D scene;
uniform sampler2D bloomBlur;
uniform float u_bloomStrength;

void main()
{
    vec3 sceneColor = texture(scene, TexCoords).rgb;
    vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;
    vec3 result = sceneColor + bloomColor * u_bloomStrength;
    FragColor = vec4(result, 1.0);
}
