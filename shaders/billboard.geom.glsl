#version 430 core

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform float Size2;           // Half the width/height of the quad
uniform vec2 SizeXY;           // Optional non-square half-size override (x, y)
uniform mat4 ProjectionMatrix;
uniform vec4 TexRect;          // UV rect: uMin, vMin, uMax, vMax in UV space
uniform int FlipX;
uniform int FlipY;

out vec2 TexCoord;

void main()
{
    // gl_in[0].gl_Position is in view space (from vertex shader)
    vec4 viewPos = gl_in[0].gl_Position;

    float sizeX = (SizeXY.x > 0.0) ? SizeXY.x : Size2;
    float sizeY = (SizeXY.y > 0.0) ? SizeXY.y : Size2;

    float uMin = TexRect.x;
    float vMin = TexRect.y;
    float uMax = TexRect.z;
    float vMax = TexRect.w;

    float uLeft = (FlipX != 0) ? uMax : uMin;
    float uRight = (FlipX != 0) ? uMin : uMax;

    float vTop = (FlipY != 0) ? vMax : vMin;
    float vBottom = (FlipY != 0) ? vMin : vMax;

    // Bottom-left vertex
    gl_Position = ProjectionMatrix * (viewPos + vec4(-sizeX, -sizeY, 0.0, 0.0));
    TexCoord = vec2(uLeft, vBottom);
    EmitVertex();

    // Bottom-right vertex
    gl_Position = ProjectionMatrix * (viewPos + vec4(sizeX, -sizeY, 0.0, 0.0));
    TexCoord = vec2(uRight, vBottom);
    EmitVertex();

    // Top-left vertex
    gl_Position = ProjectionMatrix * (viewPos + vec4(-sizeX, sizeY, 0.0, 0.0));
    TexCoord = vec2(uLeft, vTop);
    EmitVertex();

    // Top-right vertex
    gl_Position = ProjectionMatrix * (viewPos + vec4(sizeX, sizeY, 0.0, 0.0));
    TexCoord = vec2(uRight, vTop);
    EmitVertex();

    EndPrimitive();
}
