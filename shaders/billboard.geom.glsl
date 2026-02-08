#version 430 core

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform float Size2;           // Half the width/height of the quad
uniform mat4 ProjectionMatrix;
uniform vec4 TexRect;          // UV rect: x, y, width, height in UV space

out vec2 TexCoord;

void main()
{
    mat4 m = ProjectionMatrix;
    vec4 pos = gl_in[0].gl_Position;

    // Flip horizontally by swapping left/right UV coordinates
    float uLeft = TexRect.x + TexRect.z;   // Right edge becomes left
    float uRight = TexRect.x;               // Left edge becomes right
    
    // Flip vertically: swap top and bottom (OpenGL has origin at bottom-left)
    float vTop = TexRect.y + TexRect.w;     // Bottom becomes top
    float vBottom = TexRect.y;               // Top becomes bottom

    // Bottom-left vertex
    gl_Position = m * (pos + vec4(-Size2, -Size2, 0.0, 0.0));
    TexCoord = vec2(uLeft, vBottom);
    EmitVertex();

    // Bottom-right vertex
    gl_Position = m * (pos + vec4(Size2, -Size2, 0.0, 0.0));
    TexCoord = vec2(uRight, vBottom);
    EmitVertex();

    // Top-left vertex
    gl_Position = m * (pos + vec4(-Size2, Size2, 0.0, 0.0));
    TexCoord = vec2(uLeft, vTop);
    EmitVertex();

    // Top-right vertex
    gl_Position = m * (pos + vec4(Size2, Size2, 0.0, 0.0));
    TexCoord = vec2(uRight, vTop);
    EmitVertex();

    EndPrimitive();
}
