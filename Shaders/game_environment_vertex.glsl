#version 330 core

out vec2 screen_position;

void main() {
    vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    screen_position = position * 2.0 - 1.0;
    gl_Position = vec4(screen_position, 0.0, 1.0);
}
