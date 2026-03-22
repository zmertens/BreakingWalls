#version 430 core

uniform vec3 uColor;
uniform float uIntensity;
uniform float uTime;

layout(location = 0) out vec4 FragColor;

void main()
{
    float pulse = 0.78 + 0.22 * sin(uTime * 3.2);
    vec3 color = uColor * (uIntensity * pulse);
    FragColor = vec4(color, 1.0);
}
