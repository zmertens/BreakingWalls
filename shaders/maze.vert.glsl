#version 430 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aColor;

uniform mat4 uMVP;

out vec3 vColor;
out vec3 vWorldPos;
out vec2 vTexCoord;

void main()
{
    vColor = aColor;
    vWorldPos = aPosition;
    // Generate texture coordinates from world position for sprite sheet mapping
    vTexCoord = aPosition.xz * 0.15;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
