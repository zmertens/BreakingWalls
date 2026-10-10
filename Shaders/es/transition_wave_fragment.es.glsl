#version 300 es
precision mediump float;

// Level-transition effect over the composited frame. intensity fades 1 -> 0.
uniform sampler2D scene;
uniform float intensity;
uniform float phase;
uniform vec2 texel;

in vec2 fragment_uv;
out vec4 fragColor;

void main() {
    // The original wave.vert displaced sprite vertices; a full-screen quad has only four,
    // so the same sine/cosine field displaces the texture coordinates per fragment.
    vec2 amplitude = vec2(0.03, 0.03) * intensity;
    vec2 p = fragment_uv * 40.0;
    vec2 uv = fragment_uv;
    uv.x += cos(p.y + phase * 3.8) * amplitude.x + sin(p.y + phase * 6.3) * amplitude.x * 0.3;
    uv.y += sin(p.x + phase * 2.4) * amplitude.y + cos(p.x + phase * 5.2) * amplitude.y * 0.3;
    fragColor = vec4(texture(scene, clamp(uv, 0.0, 1.0)).rgb, 1.0);
}
