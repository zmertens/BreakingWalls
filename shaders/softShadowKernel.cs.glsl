#version 430 core

// Soft shadow kernel texture generator
// Creates a gaussian blur kernel for shadow softness

// This is a compute shader to generate the soft shadow kernel texture
// Can be precomputed and stored

layout (local_size_x = 8, local_size_y = 8) in;
layout (rgba32f, binding = 0) uniform image2D kernelOut;

uniform float uSigma;  // Standard deviation for gaussian

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(kernelOut);
    
    if (coord.x >= size.x || coord.y >= size.y) return;
    
    // Normalize to [-1, 1]
    vec2 uv = (vec2(coord) / vec2(size)) * 2.0 - 1.0;
    float dist = length(uv);
    
    // Gaussian falloff: e^(-x^2 / 2sigma^2)
    float gaussian = exp(-(dist * dist) / (2.0 * uSigma * uSigma));
    
    // Smooth penumbra edge for soft shadows
    float softness = smoothstep(0.0, 1.5, 1.0 - dist) * gaussian;
    
    imageStore(kernelOut, coord, vec4(vec3(softness), 1.0));
}
