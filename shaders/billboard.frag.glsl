#version 430 core

in vec2 TexCoord;

uniform sampler2D SpriteTex;
uniform vec4 TintColor;
uniform int UseRedAsAlpha;
uniform int uOITPass;
uniform float uOITWeightScale;

layout(location = 0) out vec4 OutColor;
layout(location = 1) out float OutReveal;

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
        float depth = clamp(gl_FragCoord.z, 0.0, 1.0);
        float depthWeight = max(0.05, 1.0 - depth);
        float weight = max(0.01, alpha * depthWeight * uOITWeightScale);

        OutColor = vec4(rgb * alpha * weight, alpha * weight);
        OutReveal = alpha;
    }
    else
    {
        OutColor = vec4(rgb, alpha);
        OutReveal = 0.0;
    }
}
