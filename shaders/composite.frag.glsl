#version 430 core

in vec2 vTexCoord;

layout (location = 0) out vec4 FragColor;

layout (binding = 0) uniform sampler2D uSceneTex;
layout (binding = 1) uniform sampler2D uBillboardTex;

uniform vec2 uInvResolution;
uniform float uBloomThreshold;
uniform float uBloomStrength;
uniform float uSpriteAlpha;

vec3 bloomSample(vec2 uv)
{
    vec3 base = texture(uBillboardTex, uv).rgb;
    return max(base - vec3(uBloomThreshold), vec3(0.0));
}

void main()
{
    vec4 scene = texture(uSceneTex, vTexCoord);
    vec4 sprite = texture(uBillboardTex, vTexCoord);

    float alpha = clamp(sprite.a * uSpriteAlpha, 0.0, 1.0);
    vec3 composite = mix(scene.rgb, sprite.rgb, alpha);

    vec2 texel = uInvResolution;
    vec3 bloom = bloomSample(vTexCoord) * 0.4;
    bloom += bloomSample(vTexCoord + vec2(texel.x, 0.0)) * 0.15;
    bloom += bloomSample(vTexCoord - vec2(texel.x, 0.0)) * 0.15;
    bloom += bloomSample(vTexCoord + vec2(0.0, texel.y)) * 0.15;
    bloom += bloomSample(vTexCoord - vec2(0.0, texel.y)) * 0.15;

    composite += bloom * uBloomStrength;

    FragColor = vec4(composite, 1.0);
}
