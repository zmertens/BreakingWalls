#version 430 core

layout (location = 0) in vec3 VertexPosition;

uniform mat4 ModelViewMatrix;

void main()
{
    // Transform vertex to view space (geometry shader will handle projection)
    gl_Position = ModelViewMatrix * vec4(VertexPosition, 1.0);
}
