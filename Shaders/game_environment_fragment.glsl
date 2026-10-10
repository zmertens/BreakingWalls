#version 330 core

uniform samplerCube environment_map;
uniform float rotation;
uniform float zoom;
uniform vec2 pan;
uniform vec2 viewport_scale;
// seconds, level, activated lights, board half-width in camera units
uniform vec4 animation;
in vec2 screen_position;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 brightColor;

vec3 neon_field(vec2 world) {
    vec3 radiance = vec3(0.012, 0.018, 0.055);
    vec2 drift = vec2(sin(animation.x * 0.12), cos(animation.x * 0.09)) * 0.12;
    for (int plane = 0; plane < 9; ++plane) {
        float depth = float(plane) + 1.0;
        vec2 p = world * (2.0 + depth * 0.65) + drift * depth;
        p.y += animation.x * 0.08 + animation.y * 0.11;
        vec2 edge = abs(fract(p) - 0.5);
        float line_width = max(0.012, max(fwidth(p.x), fwidth(p.y)));
        float line = 1.0 - smoothstep(0.008, 0.008 + line_width, min(edge.x, edge.y));
        float ripple = 0.55 + 0.45 * sin(depth * 1.7 + animation.x * 0.4 + length(world) * 4.0);
        vec3 tint = mix(vec3(0.04, 0.35, 0.9), vec3(0.65, 0.06, 0.5), depth / 10.0);
        radiance += tint * line * ripple * (0.16 / depth);
    }
    return radiance * (1.0 + animation.z * 0.035);
}

void main() {
    vec2 local = vec2(screen_position.x, -screen_position.y) * viewport_scale / zoom;
    float c = cos(rotation), s = sin(rotation);
    vec2 world = vec2(c * local.x - s * local.y, s * local.x + c * local.y) + pan;
    vec3 maze = texture(environment_map, normalize(vec3(world.x, -0.7, world.y))).rgb * 0.24;
    // Keep a faint maze layer near the board, fading into neon beyond its footprint.
    float board_distance = max(abs(world.x), abs(world.y)) / max(animation.w, 0.01);
    float height_band = 1.0 - smoothstep(0.65, 1.7, board_distance);
    float pulse = 0.5 + 0.5 * sin(animation.x * 0.35 + animation.y * 0.4 + board_distance * 2.0);
    float maze_weight = height_band * mix(0.10, 0.24, pulse);
    vec3 radiance = mix(neon_field(world), maze, maze_weight);
    radiance *= 1.0 - 0.2 * smoothstep(0.4, 1.4, length(screen_position));
    fragColor = vec4(radiance, 1.0);
    brightColor = vec4(0.0, 0.0, 0.0, 1.0);
}
