#version 430 core

in vec2 vTexCoord;

layout (location = 0) out vec4 FragColor;
layout (binding = 0) uniform sampler2D uInputTex;

uniform float uExposure;
uniform bool uEnableTonemap;
uniform bool uPreviewMode;

vec3 reinhardTonemap(vec3 c)
{
    return c / (vec3(1.0) + c);
}

void main()
{
    vec3 color = texture(uInputTex, vTexCoord).rgb;
    color *= max(uExposure, 0.0001);

    if (uEnableTonemap)
    {
        color = reinhardTonemap(color);
    }

    if (uPreviewMode)
    {
        // Reserved for optional preview-only grading.
    }

    // Final output remains in display-referred space for compositing.
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
