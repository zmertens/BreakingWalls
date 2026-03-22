#version 430 core
out vec2 vUV;
void main()
{
    const vec2 p[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vUV = p[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
}
