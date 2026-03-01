#version 430 core
in vec3 vNormal;
flat in uint vCellId;

layout(location = 0) out vec4 fragColor;

layout(std430, binding = 1) buffer CellColors {
    vec3 colors[];
};

void main() {
    vec3 n = normalize(vNormal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(n, lightDir), 0.0);
    vec3 base = colors[vCellId];
    vec3 col = base * (0.3 + 0.7 * diff);
    fragColor = vec4(col, 1.0);
}
