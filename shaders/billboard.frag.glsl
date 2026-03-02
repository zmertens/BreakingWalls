#version 430 core

in vec2 TexCoord;

uniform sampler2D SpriteTex;
uniform vec4 TintColor;
uniform int UseRedAsAlpha;
uniform int uOITPass;
uniform float uOITWeightScale;

<<<<<<< HEAD
layout(location = 0) out vec4 OutColor;
layout(location = 1) out float OutReveal;
=======
layout(location = 0) out vec4 outAccum;
layout(location = 1) out vec4 outReveal;
>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d

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
<<<<<<< HEAD
    
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
=======

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
>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d
    }
}
