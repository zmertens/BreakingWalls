#version 430 core

in vec2 vTexCoord;

layout (location = 0) out vec4 FragColor;

layout (binding = 0) uniform sampler2D uSceneTex;
layout (binding = 1) uniform sampler2D uBillboardTex;
layout (binding = 2) uniform sampler2D uShadowTex;          // Shadow texture
layout (binding = 3) uniform sampler2D uReflectionTex;      // Player reflection texture

uniform vec2 uInvResolution;
uniform float uBloomThreshold;
uniform float uBloomStrength;
uniform float uSpriteAlpha;
uniform float uShadowStrength;           // Shadow intensity [0-1]
uniform bool uEnableShadows;             // Toggle shadow rendering
uniform bool uEnableReflections;         // Toggle reflection rendering

vec3 bloomSample(vec2 uv)
{
    vec4 src = texture(uBillboardTex, uv);
    vec3 base = src.rgb * src.a;
    return max(base - vec3(uBloomThreshold), vec3(0.0));
}

// Compute soft shadow using gaussian blur sampling
vec4 softShadowSample(vec2 uv)
{
    const int kernelSize = 3;
    const float sigma = 1.5;
    const float pi2sigma2 = 1.0 / (2.0 * sigma * sigma);
    const float divisor = 1.0 / (2.0 * 3.14159 * sigma * sigma);
    
    vec4 shadowAccum = vec4(0.0);
    float weightSum = 0.0;
    
    // Gaussian blur kernel
    for (int y = -kernelSize; y <= kernelSize; ++y)
    {
        for (int x = -kernelSize; x <= kernelSize; ++x)
        {
            vec2 offset = vec2(x, y) * uInvResolution * 2.0;
            float weight = exp(-(float(x*x + y*y) * pi2sigma2)) * divisor;
            shadowAccum += texture(uShadowTex, uv + offset) * weight;
            weightSum += weight;
        }
    }
    
    return shadowAccum / max(weightSum, 0.001);
}

void main()
{
    vec4 scene = texture(uSceneTex, vTexCoord);
    vec4 sprite = texture(uBillboardTex, vTexCoord);

    float alpha = clamp(sprite.a * uSpriteAlpha, 0.0, 1.0);
    vec3 composite = mix(scene.rgb, sprite.rgb, alpha);

    // Apply soft shadows to the composite  
    if (uEnableShadows)
    {
        vec4 shadowSample = softShadowSample(vTexCoord);
        float shadowFactor = shadowSample.r;
        
        // Apply soft shadow with strength modulation
        // Shadow approaches 1.0 for full shadow, 0.0 for no shadow
        vec3 shadowTint = mix(composite, composite * 0.6, shadowFactor * uShadowStrength);
        composite = mix(composite, shadowTint, shadowFactor * uShadowStrength);
    }

    // Apply player reflection on ground plane
    if (uEnableReflections)
    {
        vec4 reflectionSample = texture(uReflectionTex, vTexCoord);
        if (reflectionSample.a > 0.001)
        {
            // Blend reflection with low opacity (ground plane blending)
            float reflectionAlpha = reflectionSample.a * 0.3;  // 30% opacity for reflection
            composite = mix(composite, reflectionSample.rgb, reflectionAlpha);
        }
    }

    vec2 texel = uInvResolution;
    vec3 bloom = bloomSample(vTexCoord) * 0.4;
    bloom += bloomSample(vTexCoord + vec2(texel.x, 0.0)) * 0.15;
    bloom += bloomSample(vTexCoord - vec2(texel.x, 0.0)) * 0.15;
    bloom += bloomSample(vTexCoord + vec2(0.0, texel.y)) * 0.15;
    bloom += bloomSample(vTexCoord - vec2(0.0, texel.y)) * 0.15;

    composite += bloom * uBloomStrength;

    FragColor = vec4(composite, 1.0);
}
