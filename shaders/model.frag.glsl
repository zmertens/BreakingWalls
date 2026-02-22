#version 430 core

in vec3 vWorldPos;
in vec3 vWorldNormal;

out vec4 FragColor;

uniform vec3 uSunDir;

void main()
{
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(-uSunDir);
    float ndotl = max(dot(N, L), 0.0);

    // Approximate view direction toward camera-origin in view-like framing.
    // Used only for a subtle stylized rim highlight.
    vec3 V = normalize(-vWorldPos);

    float region = 0.5 + 0.5 * sin(vWorldPos.y * 2.4 + vWorldPos.x * 0.9);
    float grain = 0.5 + 0.5 * sin(vWorldPos.x * 8.7 + vWorldPos.z * 6.3 + vWorldPos.y * 3.1);

    vec3 base = vec3(0.34, 0.24, 0.17);
    vec3 shadowTint = vec3(0.12, 0.08, 0.06);
    vec3 litTint = vec3(0.68, 0.50, 0.33);

    vec3 leatherTone = vec3(0.42, 0.27, 0.16);
    vec3 clothTone = vec3(0.28, 0.20, 0.15);
    vec3 skinTone = vec3(0.52, 0.34, 0.24);

    vec3 detailTone = mix(clothTone, leatherTone, region);
    detailTone = mix(detailTone, skinTone, clamp((vWorldPos.y - 2.0) * 0.35, 0.0, 1.0));
    base = mix(base, detailTone, 0.45 + 0.18 * grain);

    vec3 color = base * mix(shadowTint, litTint, ndotl);

    color.r *= 0.96;
    color.g *= 0.88;
    color.b *= 0.76;

    float fresnel = pow(1.0 - max(dot(N, vec3(0.0, 1.0, 0.0)), 0.0), 2.0);
    color += vec3(0.12, 0.07, 0.04) * fresnel;

    // Subtle synthwave-compatible cool rim light to separate player silhouette
    // from earthy terrain without abandoning the brown palette.
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.6);
    vec3 rimColor = vec3(0.18, 0.30, 0.42);
    color += rimColor * rim * 0.16;

    // Gentle lift on lit regions so the character reads cleaner against the scene.
    float highlight = smoothstep(0.35, 1.0, ndotl);
    color += vec3(0.07, 0.05, 0.04) * highlight;

    color *= 0.86;

    // Keep bloom contribution controlled but allow small punch-through on highlights.
    color = min(color, vec3(1.18));

    FragColor = vec4(color, 1.0);
}
