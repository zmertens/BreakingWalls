#version 330 core

// Radial fog in board space: clear over the grid, thickening into drifting noise clouds beyond it.
uniform sampler2D cloud_noise;
uniform vec2 view_size;      // viewport size in layout units
uniform float pitch;         // layout units per world unit
uniform float rotation;
uniform vec2 camera;         // world xz at the viewport centre
uniform vec2 board_center;
uniform float fog_radius;    // roughly the half-size of the grid, in world units
uniform float seconds;
uniform float density;

in vec2 fragment_uv;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 brightColor;

void main() {
    vec2 d = vec2(fragment_uv.x - 0.5, 0.5 - fragment_uv.y) * view_size / pitch;
    float c = cos(rotation), s = sin(rotation);
    vec2 world = camera + vec2(c * d.x - s * d.y, s * d.x + c * d.y);
    vec2 offset = world - board_center;
    float radial = length(offset) / fog_radius;
    // Fog and wisps are exactly zero below this radius, so skip the noise reads there.
    if (radial < 0.55) discard;
    // Two noise layers drifting in opposite directions give the clouds parallax-like depth.
    vec2 uv = world * 0.05;
    float near_layer = texture(cloud_noise, uv + vec2(seconds * 0.010, seconds * 0.004)).r;
    float far_layer = texture(cloud_noise, uv * 0.45 - vec2(seconds * 0.005, -seconds * 0.007) + 0.37).r;
    float clouds = mix(near_layer, far_layer, 0.45);
    // The noise warps the fog edge so it billows instead of forming a perfect ring.
    float edge = radial + (clouds - 0.5) * 0.55;
    float fog = smoothstep(0.85, 1.9, edge);
    float wisps = smoothstep(0.35, 0.85, clouds) * smoothstep(0.55, 1.1, radial) * 0.25;
    float alpha = clamp((fog * 0.88 + wisps) * density, 0.0, 0.92);
    vec3 shade = mix(vec3(0.42, 0.50, 0.66), vec3(0.82, 0.87, 0.95), clouds);
    fragColor = vec4(shade, alpha);
    brightColor = vec4(0.0, 0.0, 0.0, 0.0);
}
