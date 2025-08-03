#version 410

uniform vec3 colour;

out vec4 FragColour;

void main()
{
  FragColour = vec4(colour,1.0);
}
