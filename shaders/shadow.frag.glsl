#version 430 core

// Shadow volume rendering fragment shader

in vec2 vShadowCoord;

layout (location = 0) out vec4 FragColor;

void main()
{
    // Output shadow as semi-transparent black
    // Use distance from center to create soft edges
    vec2 center = vec2(0.5, 0.5);
    vec2 toCenter = vShadowCoord - center;
    float distToCenter = length(toCenter);
    
    // Create a circular soft shadow
    float shadowFalloff = exp(-distToCenter * distToCenter * 2.0);
    
    // Output shadow intensity (black with alpha for blending)
    FragColor = vec4(0.0, 0.0, 0.0, shadowFalloff * 0.8);
}
