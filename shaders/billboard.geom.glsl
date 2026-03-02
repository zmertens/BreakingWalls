#version 430 core

layout(points) in;
layout(triangle_strip, max_vertices = 8) out;

uniform float Size2;           // Half the width/height of the quad
uniform vec2 SizeXY;           // Optional non-square half-size override (x, y)
uniform mat4 ProjectionMatrix;
uniform mat4 ViewMatrix;
uniform int UseWorldAxes;
uniform vec3 RightAxisWS;
uniform vec3 UpAxisWS;
uniform int DoubleSided;
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

    vec3 rightVS = vec3(1.0, 0.0, 0.0);
    vec3 upVS = vec3(0.0, 1.0, 0.0);
    if (UseWorldAxes != 0)
    {
        rightVS = normalize(mat3(ViewMatrix) * RightAxisWS);
        upVS = normalize(mat3(ViewMatrix) * UpAxisWS);
    }

    vec3 pBL = rightVS * (-sizeX) + upVS * (-sizeY);
    vec3 pBR = rightVS * ( sizeX) + upVS * (-sizeY);
    vec3 pTL = rightVS * (-sizeX) + upVS * ( sizeY);
    vec3 pTR = rightVS * ( sizeX) + upVS * ( sizeY);

    // Front face
    gl_Position = ProjectionMatrix * (viewPos + vec4(pBL, 0.0));
    TexCoord = vec2(uLeft, vBottom);
    EmitVertex();

    gl_Position = ProjectionMatrix * (viewPos + vec4(pBR, 0.0));
    TexCoord = vec2(uRight, vBottom);
    EmitVertex();

    gl_Position = ProjectionMatrix * (viewPos + vec4(pTL, 0.0));
    TexCoord = vec2(uLeft, vTop);
    EmitVertex();

    gl_Position = ProjectionMatrix * (viewPos + vec4(pTR, 0.0));
    TexCoord = vec2(uRight, vTop);
    EmitVertex();

    EndPrimitive();

    if (DoubleSided != 0)
    {
        // Back face with reversed winding
        gl_Position = ProjectionMatrix * (viewPos + vec4(pBL, 0.0));
        TexCoord = vec2(uLeft, vBottom);
        EmitVertex();

        gl_Position = ProjectionMatrix * (viewPos + vec4(pTL, 0.0));
        TexCoord = vec2(uLeft, vTop);
        EmitVertex();

        gl_Position = ProjectionMatrix * (viewPos + vec4(pBR, 0.0));
        TexCoord = vec2(uRight, vBottom);
        EmitVertex();

        gl_Position = ProjectionMatrix * (viewPos + vec4(pTR, 0.0));
        TexCoord = vec2(uRight, vTop);
        EmitVertex();

        EndPrimitive();
    }
}
