// Random number generation utilities for path tracing
// Based on PCG hash function - better quality than fract(sin())

#ifndef RANDOM_GLSL
#define RANDOM_GLSL

// PCG hash - excellent statistical properties
uint pcg_hash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Generate random float in [0,1] range
float random(uvec2 pixel, uint sampleIndex, uint bounce) {
    uint seed = pixel.x + pixel.y * 1920u + sampleIndex * 12345u + bounce * 67890u;
    return float(pcg_hash(seed)) / float(0xffffffffu);
}

// Generate random float in [min, max] range
float randomRange(uvec2 pixel, uint sampleIndex, uint bounce, float minVal, float maxVal) {
    return minVal + (maxVal - minVal) * random(pixel, sampleIndex, bounce);
}

// Generate random vec2 in unit square [0,1]^2
vec2 random2(uvec2 pixel, uint sampleIndex, uint bounce) {
    return vec2(
        random(pixel, sampleIndex, bounce * 2u),
        random(pixel, sampleIndex, bounce * 2u + 1u)
    );
}

// Generate random vec3 in unit cube [0,1]^3
vec3 random3(uvec2 pixel, uint sampleIndex, uint bounce) {
    return vec3(
        random(pixel, sampleIndex, bounce * 3u),
        random(pixel, sampleIndex, bounce * 3u + 1u),
        random(pixel, sampleIndex, bounce * 3u + 2u)
    );
}

// Generate random point in unit sphere using rejection sampling
vec3 randomInUnitSphere(uvec2 pixel, uint sampleIndex, uint bounce) {
    // Use spherical coordinates for uniform distribution
    float theta = random(pixel, sampleIndex, bounce * 3u) * 2.0 * 3.14159265;
    float phi = acos(2.0 * random(pixel, sampleIndex, bounce * 3u + 1u) - 1.0);
    float r = pow(random(pixel, sampleIndex, bounce * 3u + 2u), 0.333);

    return vec3(
        r * sin(phi) * cos(theta),
        r * sin(phi) * sin(theta),
        r * cos(phi)
    );
}

// Generate random point on unit sphere surface
vec3 randomOnUnitSphere(uvec2 pixel, uint sampleIndex, uint bounce) {
    float theta = random(pixel, sampleIndex, bounce * 2u) * 2.0 * 3.14159265;
    float phi = acos(2.0 * random(pixel, sampleIndex, bounce * 2u + 1u) - 1.0);

    return vec3(
        sin(phi) * cos(theta),
        sin(phi) * sin(theta),
        cos(phi)
    );
}

// Generate random point in unit hemisphere (normal oriented)
vec3 randomInHemisphere(uvec2 pixel, uint sampleIndex, uint bounce, vec3 normal) {
    vec3 inUnitSphere = randomInUnitSphere(pixel, sampleIndex, bounce);
    if (dot(inUnitSphere, normal) > 0.0) {
        return inUnitSphere;
    } else {
        return -inUnitSphere;
    }
}

// Generate random point in unit disk (for depth of field)
vec2 randomInUnitDisk(uvec2 pixel, uint sampleIndex, uint bounce) {
    for (int i = 0; i < 10; i++) {
        vec2 p = 2.0 * random2(pixel, sampleIndex, bounce + uint(i)) - 1.0;
        if (dot(p, p) < 1.0) {
            return p;
        }
    }
    return vec2(0.0);
}

#endif // RANDOM_GLSL

