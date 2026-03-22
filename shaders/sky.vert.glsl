#version 430 core

out vec2 vNDC;

void main()
{
    const vec2 p[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vNDC = p[gl_VertexID];
    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
}
