#version 450 core

// Path tracing compute shader with progressive refinement
// Based on physically-based rendering principles

#define MAX_SPHERES 20
#define MAX_LIGHTS 5
#define MAX_BOUNCES 8
#define EPSILON 0.001

// Material types (must match C++ MaterialType enum)
#define LAMBERTIAN 0
#define METAL 1
#define DIELECTRIC 2

// ============================================================================
// Random Number Generation (PCG Hash)
// ============================================================================

uint pcg_hash(uint seed) {
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(uvec2 pixel, uint sampleIndex, uint bounce) {
    uint seed = pixel.x + pixel.y * 1920u + sampleIndex * 12345u + bounce * 67890u;
    return float(pcg_hash(seed)) / float(0xffffffffu);
}

vec2 random2(uvec2 pixel, uint sampleIndex, uint bounce) {
    return vec2(
        random(pixel, sampleIndex, bounce * 2u),
        random(pixel, sampleIndex, bounce * 2u + 1u)
    );
}

vec3 randomInUnitSphere(uvec2 pixel, uint sampleIndex, uint bounce) {
    float theta = random(pixel, sampleIndex, bounce * 3u) * 2.0 * 3.14159265;
    float phi = acos(2.0 * random(pixel, sampleIndex, bounce * 3u + 1u) - 1.0);
    float r = pow(random(pixel, sampleIndex, bounce * 3u + 2u), 0.333);

    return vec3(
        r * sin(phi) * cos(theta),
        r * sin(phi) * sin(theta),
        r * cos(phi)
    );
}

// Image textures for accumulation and display
layout (binding = 0, rgba32f) uniform image2D uAccumTexture;  // Accumulated samples
layout (binding = 1, rgba32f) uniform image2D uDisplayTexture; // Current display (gamma corrected)

// Camera structure
struct Camera {
    vec3 eye;
    float far;
    vec3 ray00;
    vec3 ray01;
    vec3 ray10;
    vec3 ray11;
};

// Ray structure
struct Ray {
    vec3 origin;
    vec3 direction;
};

// PBR Sphere structure (must match C++ Sphere.hpp layout)
struct Sphere {
    vec4 center;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
    float radius;
    float radius2;
    float shininess;
    float reflectivity;
    vec4 albedo;
    uint materialType;
    float fuzz;
    float refractiveIndex;
    uint padding;
};

// Hit record for ray intersections
struct HitRecord {
    vec3 point;
    vec3 normal;
    float t;
    bool frontFace;
    uint materialType;
    vec3 albedo;
    float fuzz;
    float refractiveIndex;
};

// Uniforms
uniform Camera uCamera;
uniform uint uBatch;
uniform uint uSamplesPerBatch;

// Shader storage buffer for spheres
layout (std430, binding = 1) buffer SphereBuffer {
    Sphere bSpheres[MAX_SPHERES];
};

// ============================================================================
// Intersection Functions
// ============================================================================

bool sphereIntersect(in Sphere sphere, in Ray ray, float tMin, float tMax, out HitRecord hit) {
    vec3 oc = ray.origin - sphere.center.xyz;
    float a = dot(ray.direction, ray.direction);
    float halfB = dot(oc, ray.direction);
    float c = dot(oc, oc) - sphere.radius2;

    float discriminant = halfB * halfB - a * c;
    if (discriminant < 0.0) return false;

    float sqrtD = sqrt(discriminant);

    // Find nearest root in acceptable range
    float root = (-halfB - sqrtD) / a;
    if (root < tMin || root > tMax) {
        root = (-halfB + sqrtD) / a;
        if (root < tMin || root > tMax) {
            return false;
        }
    }

    hit.t = root;
    hit.point = ray.origin + ray.direction * root;
    vec3 outwardNormal = (hit.point - sphere.center.xyz) / sphere.radius;
    hit.frontFace = dot(ray.direction, outwardNormal) < 0.0;
    hit.normal = hit.frontFace ? outwardNormal : -outwardNormal;
    hit.materialType = sphere.materialType;
    hit.albedo = sphere.albedo.rgb;
    hit.fuzz = sphere.fuzz;
    hit.refractiveIndex = sphere.refractiveIndex;

    return true;
}

bool hitWorld(in Ray ray, float tMin, float tMax, out HitRecord hit) {
    HitRecord tempHit;
    bool hitAnything = false;
    float closest = tMax;

    for (int i = 0; i < MAX_SPHERES; i++) {
        if (sphereIntersect(bSpheres[i], ray, tMin, closest, tempHit)) {
            hitAnything = true;
            closest = tempHit.t;
            hit = tempHit;
        }
    }

    return hitAnything;
}

// ============================================================================
// Material Scattering Functions
// ============================================================================

// Schlick's approximation for reflectance
float schlickReflectance(float cosine, float refractiveRatio) {
    float r0 = (1.0 - refractiveRatio) / (1.0 + refractiveRatio);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

// Scatter ray based on material type
bool scatter(in HitRecord hit, in Ray rayIn, out vec3 attenuation, out Ray scattered,
             uvec2 pixel, uint sampleIndex, uint bounce) {

    // LAMBERTIAN (Diffuse)
    if (hit.materialType == LAMBERTIAN) {
        vec3 scatterDir = hit.normal + randomInUnitSphere(pixel, sampleIndex, bounce);

        // Catch degenerate scatter direction
        if (abs(scatterDir.x) < EPSILON && abs(scatterDir.y) < EPSILON && abs(scatterDir.z) < EPSILON) {
            scatterDir = hit.normal;
        }

        scattered = Ray(hit.point, normalize(scatterDir));
        attenuation = hit.albedo;
        return true;
    }

    // METAL (Reflective)
    if (hit.materialType == METAL) {
        vec3 reflected = reflect(normalize(rayIn.direction), hit.normal);
        reflected += hit.fuzz * randomInUnitSphere(pixel, sampleIndex, bounce);
        scattered = Ray(hit.point, normalize(reflected));
        attenuation = hit.albedo;
        return dot(scattered.direction, hit.normal) > 0.0;
    }

    // DIELECTRIC (Glass)
    if (hit.materialType == DIELECTRIC) {
        attenuation = vec3(1.0);
        float ri = hit.frontFace ? (1.0 / hit.refractiveIndex) : hit.refractiveIndex;

        vec3 unitDir = normalize(rayIn.direction);
        float cosTheta = min(dot(-unitDir, hit.normal), 1.0);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

        bool cannotRefract = ri * sinTheta > 1.0;
        float reflectProb = schlickReflectance(cosTheta, ri);

        vec3 direction;
        if (cannotRefract || reflectProb > random(pixel, sampleIndex, bounce * 100u + 50u)) {
            direction = reflect(unitDir, hit.normal);
        } else {
            direction = refract(unitDir, hit.normal, ri);
        }

        scattered = Ray(hit.point, direction);
        return true;
    }

    return false;
}

// ============================================================================
// Sky / Background
// ============================================================================

vec3 skyColor(vec3 direction) {
    float t = 0.5 * (normalize(direction).y + 1.0);
    return mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
}

// ============================================================================
// Path Tracing
// ============================================================================

vec3 traceRay(Ray ray, uvec2 pixel, uint sampleIndex) {
    vec3 color = vec3(1.0);

    for (uint bounce = 0; bounce < MAX_BOUNCES; bounce++) {
        HitRecord hit;

        if (hitWorld(ray, EPSILON, uCamera.far, hit)) {
            vec3 attenuation;
            Ray scattered;

            if (scatter(hit, ray, attenuation, scattered, pixel, sampleIndex, bounce)) {
                color *= attenuation;
                ray = scattered;
            } else {
                // Absorbed
                return vec3(0.0);
            }
        } else {
            // Hit sky
            color *= skyColor(ray.direction);
            return color;
        }

        // Russian roulette termination for performance
        if (bounce > 3) {
            float p = max(color.r, max(color.g, color.b));
            if (random(pixel, sampleIndex, bounce * 1000u) > p) {
                return vec3(0.0);
            }
            color /= p;
        }
    }

    return vec3(0.0); // Exceeded max bounces
}

// ============================================================================
// Main
// ============================================================================

layout (local_size_x = 20, local_size_y = 20) in;

void main() {
    uvec2 pixel = gl_GlobalInvocationID.xy;
    uvec2 size = imageSize(uDisplayTexture);

    if (pixel.x >= size.x || pixel.y >= size.y) return;

    vec3 accumulatedColor = vec3(0.0);

    // Multiple samples per batch for progressive refinement
    uint startSample = uBatch * uSamplesPerBatch;
    for (uint s = 0; s < uSamplesPerBatch; s++) {
        uint sampleIndex = startSample + s;

        // Anti-aliasing: jitter pixel position
        vec2 jitter = random2(pixel, sampleIndex, 0u) - 0.5;
        vec2 pixelPos = (vec2(pixel) + jitter) / vec2(size);

        // Compute camera ray using frustum corners
        vec3 cameraDir = mix(
            mix(uCamera.ray00, uCamera.ray01, pixelPos.y),
            mix(uCamera.ray10, uCamera.ray11, pixelPos.y),
            pixelPos.x
        );

        Ray ray = Ray(uCamera.eye, normalize(cameraDir));

        // Trace the ray
        vec3 color = traceRay(ray, pixel, sampleIndex);
        accumulatedColor += color;
    }

    // Accumulate with previous batches
    if (uBatch > 0) {
        vec3 previous = imageLoad(uAccumTexture, ivec2(pixel)).rgb;
        accumulatedColor += previous;
    }

    imageStore(uAccumTexture, ivec2(pixel), vec4(accumulatedColor, 1.0));

    // Compute average and apply gamma correction for display
    uint totalSamples = (uBatch + 1) * uSamplesPerBatch;
    vec3 averageColor = accumulatedColor / float(totalSamples);

    // Gamma correction (gamma = 2.0)
    averageColor = sqrt(averageColor);

    imageStore(uDisplayTexture, ivec2(pixel), vec4(averageColor, 1.0));
}

