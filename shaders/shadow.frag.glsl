#version 430 core

// Shadow volume rendering fragment shader

in vec2 vShadowCoord;

layout (location = 0) out vec4 FragColor;

void main()
{
    // Calculate soft shadow intensity using distance from quad center
    vec2 centerOffset = vShadowCoord - vec2(0.5, 0.5);
    float distFromCenter = length(centerOffset);
    
    // Exponential falloff for soft edges
    float shadowIntensity = exp(-distFromCenter * distFromCenter * 3.0);
    
    // Output shadow intensity in red channel (composite.frag reads .r)
    FragColor = vec4(shadowIntensity, 0.0, 0.0, 1.0);
}
