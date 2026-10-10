#version 330 core

// Level-transition effect over the composited frame. intensity fades 1 -> 0.
uniform sampler2D scene;
uniform float intensity;
uniform float phase;
uniform vec2 texel;

in vec2 fragment_uv;
out vec4 fragColor;

void main() {
    // Original 3x3 tent kernel (weights sum to 16); only the radius changes over time.
    float radius = intensity * 6.0;
    vec2 offx = vec2(texel.x * radius, 0.0);
    vec2 offy = vec2(0.0, texel.y * radius);
    vec2 uv = fragment_uv;
    vec3 pixel = texture(scene, uv).rgb * 4.0 +
                 (texture(scene, uv - offx).rgb + texture(scene, uv + offx).rgb +
                  texture(scene, uv - offy).rgb + texture(scene, uv + offy).rgb) * 2.0 +
                 texture(scene, uv - offx - offy).rgb + texture(scene, uv - offx + offy).rgb +
                 texture(scene, uv + offx - offy).rgb + texture(scene, uv + offx + offy).rgb;
    fragColor = vec4(pixel / 16.0, 1.0);
}
