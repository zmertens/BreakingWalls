#version 430 core

in vec2 vTexCoord;

layout (location = 0) out vec4 FragColor;
layout (binding = 0) uniform sampler2D uInputTex;

void main()
{
    FragColor = texture(uInputTex, vTexCoord);
}
