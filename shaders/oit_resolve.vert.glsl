#version 460 core

out vec2 vTexCoord;

void main()
{
	const vec4 vertices[] = vec4[](vec4(-1.0, -1.0, 0.0, 1.0),
								   vec4( 1.0, -1.0, 0.0, 1.0),
								   vec4(-1.0,  1.0, 0.0, 1.0),
								   vec4( 1.0,  1.0, 0.0, 1.0));

	// Interpolate texture coordinates to range [0, 1]
	vTexCoord = vertices[gl_VertexID].xy * 0.5 + vec2(0.5, 0.5);

	gl_Position = vertices[gl_VertexID];
}
