#version 330 core

// Level-transition effect over the composited frame. intensity fades 1 -> 0.
uniform sampler2D scene;
uniform float intensity;
uniform float phase;
uniform vec2 texel;

in vec2 fragment_uv;
out vec4 fragColor;

void main() {
    // Same quantization as the original pixelate.frag, with the threshold driven by intensity.
    float factor = 1.0 / (intensity * 0.04 + 0.001);
    vec2 pos = (floor(fragment_uv * factor) + 0.5) / factor;
    fragColor = vec4(texture(scene, clamp(pos, 0.0, 1.0)).rgb, 1.0);
}
