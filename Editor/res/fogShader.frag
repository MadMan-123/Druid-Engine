#version 400
out vec4 FragColor;
in vec3 v_norm;
in vec4 v_pos; 

uniform vec3 fogColor;

uniform float maxDist; //fog max distance
uniform float minDist; //fog min distance


void main() 
{
float dist = abs( v_pos.z ); //absolute value (distance from 0) issue???
float fogFactor = (maxDist - dist)/(maxDist - minDist);
fogFactor = clamp( fogFactor, 0.0, 1.0 ); // constrain range
vec3 lightColor = vec3(0.8,0.8,0.8);
vec3 color = mix( fogColor, lightColor, 1-fogFactor); // mix not add

FragColor = vec4(color, 1.0);

}
