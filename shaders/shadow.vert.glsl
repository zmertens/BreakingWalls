#version 430 core

// Shadow volume rendering vertex shader - simple pass-through
// All actual projection work happens in geometry shader

void main()
{
    // Geometry shader will handle all positioning and projection
    // Vertex shader just needs to pass through one vertex
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
