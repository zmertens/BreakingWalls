#version 430 core

in vec2 TexCoord;

uniform sampler2D SpriteTex;
uniform vec4 TintColor;
uniform int UseRedAsAlpha;
uniform int uOITPass;
uniform float uOITWeightScale;

layout(location = 0) out vec4 FragColor;

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
    
    FragColor = vec4(rgb, alpha);
}
