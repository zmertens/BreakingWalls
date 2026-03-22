#version 430 core

in vec3 vWorldNormal;
in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform float uTime;
uniform int uShadowPass;

layout(location = 0) out vec4 FragColor;

vec3 sampleSynthSky(vec3 dir)
{
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 zenith = vec3(0.06, 0.04, 0.18);
    vec3 horizon = vec3(0.92, 0.24, 0.55);
    vec3 nadir = vec3(0.03, 0.02, 0.08);
    vec3 sky = mix(nadir, mix(horizon, zenith, t), smoothstep(0.0, 0.35, t));

    vec2 sunPos = vec2(0.35, 0.28);
    vec2 p = normalize(max(abs(dir.xy), vec2(0.0001))) * length(dir.xy);
    float d = length(p - sunPos);
    float sun = exp(-d * 12.0);
    sky += vec3(1.0, 0.55, 0.16) * sun * 0.6;
    return sky;
}

void main()
{
    if (uShadowPass != 0)
    {
        FragColor = vec4(0.03, 0.03, 0.05, 0.36);
        return;
    }

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    float diff = max(dot(N, lightDir), 0.0);
    float amb  = 0.28;
    vec3 baseColor = vec3(0.78, 0.82, 0.88);

    vec3 reflectDir = reflect(-V, N);
    vec3 refractDir = refract(-V, N, 1.0 / 1.18);
    vec3 reflected = sampleSynthSky(normalize(reflectDir));
    vec3 refracted = sampleSynthSky(normalize(mix(refractDir, -V, 0.25)));

    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.5);
    float shimmer = 0.92 + 0.08 * sin(uTime * 2.4 + vWorldPos.y * 5.0 + vWorldPos.x * 2.0);
    vec3 transmission = mix(baseColor, refracted, 0.55);
    vec3 specular = reflected * mix(0.18, 1.0, fresnel) * shimmer;
    vec3 color = transmission * (amb + diff * 0.52) + specular * 0.95;
    color += vec3(0.35, 0.95, 0.85) * fresnel * 0.18;
    color = mix(color, reflected, fresnel * 0.45);
    FragColor = vec4(color, 1.0);
}
