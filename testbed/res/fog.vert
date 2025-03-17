#version 410

layout (location = 0) in vec3 position;


uniform mat4 transform;

out vec4 v_pos;
out vec3 current;

void main()
{
	current = position;
	v_pos = transform * vec4(position,1.0);

	gl_Position = v_pos;
}
