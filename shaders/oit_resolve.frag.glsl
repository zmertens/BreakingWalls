#version 430 core

in vec2 vTexCoord;

layout (location = 0) out vec4 FragColor;

layout (binding = 0) uniform sampler2D uOITAccumTex;
layout (binding = 1) uniform sampler2D uOITRevealTex;

void main()
{
    vec4 accum = texture(uOITAccumTex, vTexCoord);
    float reveal = clamp(texture(uOITRevealTex, vTexCoord).r, 0.0, 1.0);

    vec3 color = accum.rgb / max(accum.a, 1e-5);
    float alpha = 1.0 - reveal;

    FragColor = vec4(color, alpha);
}
