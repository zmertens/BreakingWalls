#version 430 core

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform vec3 uSunDir;
uniform sampler2D uAlbedoTex;
uniform int uUseAlbedoTex;
uniform int uHasTexCoord;
uniform vec2 uAlbedoUVScale;

void main()
{
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(-uSunDir);
    float ndotl = max(dot(N, L), 0.0);
    vec3 V = normalize(-vWorldPos);
    vec3 H = normalize(L + V);

    float region = 0.5 + 0.5 * sin(vWorldPos.y * 2.4 + vWorldPos.x * 0.9);
    float grain = 0.5 + 0.5 * sin(vWorldPos.x * 8.7 + vWorldPos.z * 6.3 + vWorldPos.y * 3.1);

    vec3 base = vec3(0.31, 0.66, 0.80);
    vec3 shadowTint = vec3(0.14, 0.20, 0.36);
    vec3 litTint = vec3(0.55, 0.94, 1.00);

    vec3 cyanTone = vec3(0.36, 0.90, 0.98);
    vec3 lavenderTone = vec3(0.78, 0.72, 0.98);
    vec3 mintTone = vec3(0.63, 0.94, 0.86);

    vec3 detailTone = mix(cyanTone, lavenderTone, region);
    detailTone = mix(detailTone, mintTone, clamp((vWorldPos.y - 2.0) * 0.35, 0.0, 1.0));
    base = mix(base, detailTone, 0.45 + 0.18 * grain);

    vec3 textureAlbedo = vec3(1.0);
    if (uUseAlbedoTex != 0) {
        vec2 sampleUV = (uHasTexCoord != 0)
            ? fract(vTexCoord * uAlbedoUVScale)
            : fract(vWorldPos.xz * 0.12 * uAlbedoUVScale);
        vec4 texel = texture(uAlbedoTex, sampleUV);
        textureAlbedo = mix(vec3(1.0), texel.rgb, texel.a);
    }

    vec3 color = base * mix(shadowTint, litTint, ndotl);
    color = mix(color, textureAlbedo * mix(shadowTint, litTint, ndotl), 0.92);

    float fresnel = pow(1.0 - max(dot(N, vec3(0.0, 1.0, 0.0)), 0.0), 2.0);
    color += vec3(0.22, 0.40, 0.60) * fresnel;

    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.2);
    vec3 rimColor = vec3(0.44, 0.90, 1.20);
    color += rimColor * rim * 0.24;

    float spec = pow(max(dot(N, H), 0.0), 72.0);
    vec3 specularColor = vec3(0.72, 0.98, 1.35);
    color += specularColor * spec * (0.22 + 0.78 * ndotl);

    float highlight = smoothstep(0.30, 1.0, ndotl);
    color += vec3(0.08, 0.16, 0.24) * highlight;

    color *= 0.94;

    color = min(color, vec3(1.35));

    FragColor = vec4(color, 1.0);
}
