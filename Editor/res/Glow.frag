#version 400

in vec3 vN;
 
layout( location = 0 ) out vec4 fragColour;
 
 
void main() {
 
 	float intensity = pow(0.5 - dot(vN, vec3(1.0,1.0,1.0)),1.0);

    fragColour = vec4(0.7,0.1,0.1,intensity);
}