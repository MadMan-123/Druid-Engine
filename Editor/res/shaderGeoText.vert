//Version Number
#version 420
	
//The layout qualifers
layout (location = 0) in vec3 VertexPosition;
layout (location = 1) in vec2 TexCoord;
layout (location = 2) in vec3 VertexNormal;

out VS_OUT {
	vec2 texCoords;
} vs_out;

//CoreShaderData UBO (binding = 0)
layout (std140, binding = 0) uniform CoreShaderData
{
    vec3 camPos;
    float time;
    mat4 view;
    mat4 projection;
};

//Uniform variable
uniform mat4 model;


//Passing out the normal and position data
out vec3 v_norm;
out vec4 v_pos; 

void main()
{
	//Assigning the normal and position data
	v_norm = VertexNormal;
	v_pos = vec4(VertexPosition, 1.0);

	vs_out.texCoords = TexCoord;
	// Sets the position of the current vertex
	gl_Position = projection * view * model * vec4(VertexPosition, 1.0);
}