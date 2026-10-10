#version 300 es
precision highp float;

uniform sampler2D accumulation;
uniform sampler2D optical_depth;
in vec2 fragment_uv;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 brightColor;

void main() {
    vec4 sum = texture(accumulation, fragment_uv);
    float opacity = 1.0 - exp(-texture(optical_depth, fragment_uv).r * 16.0);
    vec3 color = sum.rgb / max(sum.a, 0.0001);
    fragColor = vec4(color, opacity);
    brightColor = vec4(0.0, 0.0, 0.0, opacity);
}
