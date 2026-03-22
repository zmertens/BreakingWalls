#version 430 core

in vec2 vLocal;

uniform vec3 uCellColor;
uniform float uHighlightStrength;
uniform float uTime;

layout(location = 0) out vec4 FragColor;

void main()
{
    float d = clamp(length(vLocal), 0.0, 1.4142);
    float radial = 1.0 - smoothstep(0.10, 1.38, d);
    float rim = 1.0 - smoothstep(0.72, 1.15, d);
    float pulse = 0.90 + 0.10 * sin(uTime * 4.2);
    vec3 neon = mix(uCellColor, vec3(0.92, 1.0, 0.96), 0.58);
    vec3 color = neon * (0.90 + 0.55 * radial + 0.85 * rim);
    float alpha = clamp((0.32 * radial + 0.55 * rim) * pulse * uHighlightStrength, 0.0, 0.95);
    FragColor = vec4(color, alpha);
}
