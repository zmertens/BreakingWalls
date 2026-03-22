#version 430 core

in vec2 vNDC;

layout (location = 0) out vec4 FragColor;

void main()
{
    float t = clamp(vNDC.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 zenith = vec3(0.06, 0.04, 0.18);
    vec3 horizon = vec3(0.92, 0.24, 0.55);
    vec3 nadir = vec3(0.03, 0.02, 0.08);
    vec3 sky = mix(nadir, mix(horizon, zenith, t), smoothstep(0.0, 0.35, t));

    vec2 sunPos = vec2(0.35, 0.28);
    float d = length(vNDC - sunPos);
    float sun = exp(-d * 12.0);
    sky += vec3(1.0, 0.55, 0.16) * sun * 0.6;

    FragColor = vec4(sky, 1.0);
}
