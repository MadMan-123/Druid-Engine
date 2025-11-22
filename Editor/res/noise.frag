#version 400

uniform float time;

uniform float maxDist; //fog max distance
uniform float minDist; //fog min distance
uniform float fogDensity;
uniform vec3 fogColor;

uniform sampler2D texture1;
uniform sampler2D texture2;


in vec2 vUv;
in vec4 v_pos;
out vec4 FragColour;

void main() {

	vec2 position = - 1.0 + 2.0 * vUv;

	vec4 noise = texture2D( texture1, vUv ); //noise texture
	vec2 T1 = vUv + vec2( 1.5, - 1.5 ) *  0.2 * time; // "time" 'animates' the texture
	vec2 T2 = vUv + vec2( - 0.5, 2.0 ) *  0.1 * time; // as above

	T1.x += noise.x * 4; //offset change these 4 values to see the change in frequency, see notes at end of shader.
	T1.y += noise.y * 2;
	T2.x -= noise.y * 0.2; //this just offsets the texture coordinates
	T2.y += noise.z * 0.2; // but allows us to offset y&z in opposite directions

	float p = texture2D( texture1, T1).a; //get the alpha from the noise texture

	vec4 color = texture2D( texture2, T2); //coloured texture offset can here or above
				
	vec4 temp = color * ( vec4(p) * 2.0 ) + color; //add/remove the last colour

	if( temp.r > 1.0 ) { temp.bg += clamp( temp.r - 2.0, 0.0, 100.0 ); } // again play about with these
	if( temp.g > 1.0 ) { temp.rb += temp.g - 1.0; }
	if( temp.b > 1.0 ) { temp.rg += temp.b - 1.0; } // = vec2(0.0,0.0)

	FragColour = temp;

	float dist = abs( v_pos.z ); //absolute value
	float fogFactor = (maxDist - dist)/(maxDist - minDist);
	fogFactor = clamp( fogFactor, 0.0, 1.0 ); // constrain range

	FragColour = mix( FragColour, vec4( fogColor, FragColour.w ), fogFactor );

}

//A quad with the texture coordinates 0,0 on the one corner and 2,2 on the other corner, 
//means the texture is tiled 4 times, 2 times in each direction.
//
//Now if you have the same quad with 4,4 on the other corner instead you have the texture 
//tiled 16 times, 4 times in each direction.
//
//If you increase the texture coordinates without increasing the size of the quad you are 
//drawing a larger portion of the texture on the same surface, thus making the texture itself 
//look smaller. AND in our case increasing the frequncy (amount) of alpha channel information.
