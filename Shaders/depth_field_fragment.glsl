#version 330 core

uniform float seconds;
uniform float level;
uniform float lights;
in vec2 fragment_uv;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 brightColor;

void main() {
    vec2 screen = fragment_uv * 2.0 - 1.0;
    vec3 radiance = vec3(0.012, 0.018, 0.055);
    for (int plane = 0; plane < 9; ++plane) {
        float depth = float(plane) + 1.0;
        vec2 drift = vec2(sin(seconds * 0.12), cos(seconds * 0.09)) * 0.12;
        vec2 p = screen * (2.0 + depth * 0.65) + drift * depth;
        p.y += seconds * 0.08 + level * 0.11;
        vec2 edge = abs(fract(p) - 0.5);
        float line = 1.0 - smoothstep(0.008, 0.035, min(edge.x, edge.y));
        float ripple = 0.55 + 0.45 * sin(depth * 1.7 + seconds * 0.4 + length(screen) * 4.0);
        vec3 tint = mix(vec3(0.04, 0.35, 0.9), vec3(0.65, 0.06, 0.5), depth / 10.0);
        radiance += tint * line * ripple * (0.085 / depth);
    }
    radiance *= 1.0 + lights * 0.035;
    radiance *= 1.0 - 0.25 * smoothstep(0.3, 1.4, length(screen));
    fragColor = vec4(radiance, 1.0);
    brightColor = vec4(0.0, 0.0, 0.0, 1.0);
}
