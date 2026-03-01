#version 430 core

in vec2 TexCoord;

uniform sampler2D SpriteTex;
uniform vec4 TintColor;
uniform int UseRedAsAlpha;
uniform int uOITPass;
uniform float uOITWeightScale;

layout(location = 0) out vec4 outAccum;
layout(location = 1) out vec4 outReveal;

void main()
{
    vec4 texColor = texture(SpriteTex, TexCoord);

    float coverage = (UseRedAsAlpha != 0) ? texColor.r : texColor.a;
    vec3 baseRgb = (UseRedAsAlpha != 0) ? vec3(1.0) : texColor.rgb;
    vec3 rgb = baseRgb * TintColor.rgb;
    float alpha = coverage * TintColor.a;

    // Alpha testing - discard transparent pixels
    if (alpha < 0.1)
        discard;

    if (uOITPass != 0)
    {
        // Weighted blended OIT accumulation pass
        // outAccum receives weighted color; outReveal receives alpha for the
        // multiplicative blend (GL_ZERO, GL_ONE_MINUS_SRC_COLOR) that accumulates
        // the product of (1 - alpha) across all transparent fragments.
        float weight = clamp(pow(alpha * uOITWeightScale, 2.0), 0.01, 300.0);
        outAccum = vec4(rgb * alpha * weight, alpha * weight);
        outReveal = vec4(alpha, 0.0, 0.0, 0.0);
    }
    else
    {
        outAccum = vec4(rgb, alpha);
        outReveal = vec4(0.0);
    }
}
