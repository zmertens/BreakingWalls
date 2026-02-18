#version 450 core

// Path tracing compute shader with progressive refinement
// Based on physically-based rendering principles

#define MAX_SPHERES 2000  // Increased for dynamic spawning
#define MAX_LIGHTS 5
#define MAX_BOUNCES 8
#define EPSILON 0.001

// Starfield parameters
#define PI 3.14159265359
#define TWO_PI 6.28318530718
#define FLIGHT_SPEED 8.0
#define STAR_SIZE 0.6
#define STAR_CORE_SIZE 0.14
#define STAR_THRESHOLD 0.775
#define BLACK_HOLE_THRESHOLD 0.9995
#define BLACK_HOLE_DISTORTION 0.03

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
    uint seed = (pixel.x * 73856093u) ^ (pixel.y * 19349663u) ^
                (sampleIndex * 83492791u) ^ (bounce * 2654435761u);
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
uniform uint uSphereCount;  // Actual number of spheres to check
uniform vec3 uGroundPlanePoint;
uniform vec3 uGroundPlaneNormal;
uniform vec3 uGroundPlaneAlbedo;
uniform uint uGroundPlaneMaterialType;
uniform float uGroundPlaneFuzz;
uniform float uGroundPlaneRefractiveIndex;
uniform float uTime;
uniform sampler2D uNoiseTex;

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

    for (uint i = 0; i < uSphereCount; i++) {
        if (sphereIntersect(bSpheres[i], ray, tMin, closest, tempHit)) {
            hitAnything = true;
            closest = tempHit.t;
            hit = tempHit;
        }
    }

    return hitAnything;
}

bool planeIntersect(in Ray ray, float tMin, float tMax, out HitRecord hit) {
    vec3 planeNormal = normalize(uGroundPlaneNormal);
    float denom = dot(planeNormal, ray.direction);

    if (abs(denom) < EPSILON) {
        return false;
    }

    float t = dot(uGroundPlanePoint - ray.origin, planeNormal) / denom;
    if (t < tMin || t > tMax) {
        return false;
    }

    hit.t = t;
    hit.point = ray.origin + ray.direction * t;
    hit.frontFace = dot(ray.direction, planeNormal) < 0.0;
    hit.normal = hit.frontFace ? planeNormal : -planeNormal;
    hit.materialType = uGroundPlaneMaterialType;
    hit.albedo = uGroundPlaneAlbedo;
    hit.fuzz = uGroundPlaneFuzz;
    hit.refractiveIndex = uGroundPlaneRefractiveIndex;

    return true;
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

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float noise2D(vec2 uv) {
    return texture(uNoiseTex, fract(uv)).r;
}

float hash12(vec2 p) {
    vec2 h = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(h.x + h.y) * 43758.5453123);
}

vec2 hash22(vec2 p) {
    return vec2(hash12(p), hash12(p + vec2(19.19, 73.41)));
}

vec3 getStarGlowColor(float starDistance, float angle, float hue) {
    float progress = 1.0 - starDistance;
    float spikes = mix(pow(abs(sin(angle * 2.5)), 8.0), 1.0, progress);
    return hsv2rgb(vec3(hue, 0.3, 1.0)) * (0.4 * progress * progress * spikes);
}

vec3 starfieldColor(vec3 direction, uvec2 pixel, uint sampleIndex) {
    vec3 dir = normalize(direction);

    // Spherical projection for stable infinite starfield
    float lon = atan(dir.z, dir.x);
    float lat = asin(clamp(dir.y, -1.0, 1.0));
    vec2 uv = vec2(lon / TWO_PI + 0.5, lat / PI + 0.5);

    float flight = uTime * FLIGHT_SPEED;

    // Forward (mostly straight) tunnel-like motion inspired by Shadertoy sample.
    // We avoid strong lateral panning so movement reads as "flying ahead".
    vec2 tunnelUv = dir.xy / max(0.2, dir.z + 1.15);
    vec3 tunnelCol = vec3(0.0);
    float s = 0.0;
    float v = 0.0;
    vec3 init = vec3(0.0, 0.0, flight * 0.002);
    for (int r = 0; r < 28; ++r)
    {
        vec3 p = init + s * vec3(tunnelUv, 0.055);
        p.z = fract(p.z);

        for (int i = 0; i < 5; ++i)
        {
            float d = max(dot(p, p), 1e-4);
            p = abs(p * 2.04) / d - 0.9;
        }

        float pd = max(dot(p, p), 1e-4);
        v += pow(pd, 0.7) * 0.048;
        tunnelCol += vec3(v * 0.22 + 0.36, 8.0 - s * 1.8, 0.12 + v) * v * 0.00004;
        s += 0.032;
    }
    tunnelCol = tanh(tunnelCol);

    // Keep a light world-space drift so stars still feel cosmic, but not aimless.
    vec2 flow = vec2(0.0, -flight * 0.0025);
    vec2 warpedUv = uv + flow;

    // Simulated black-hole lensing regions (localized distortion only)
    vec2 bhCell = floor(warpedUv * vec2(24.0, 12.0));
    vec2 bhRnd = hash22(bhCell + vec2(91.7, 13.3));
    float bhPresence = step(BLACK_HOLE_THRESHOLD, 0.98 + 0.02 * bhRnd.x);
    vec2 bhCenter = (bhCell + bhRnd) / vec2(24.0, 12.0);
    vec2 toBH = bhCenter - warpedUv;
    float bhDist = length(toBH * vec2(24.0, 12.0));
    if (bhPresence > 0.5 && bhDist < 0.65) {
        warpedUv += normalize(toBH + vec2(1e-4)) * (BLACK_HOLE_DISTORTION * (0.65 - bhDist));
    }

    // Star clusters and sparse thresholds
    vec2 clusterUv = warpedUv * vec2(120.0, 60.0);
    vec2 cell = floor(clusterUv);
    vec2 local = fract(clusterUv) - 0.5;

    float clusterA = noise2D(cell * 0.007 + vec2(0.724, 0.111));
    float clusterB = noise2D(cell.yx * 0.009 + vec2(0.333, 0.777));
    float clusterMask = step(STAR_THRESHOLD, clusterA) * step(STAR_THRESHOLD, clusterB);

    vec2 starRnd = hash22(cell + vec2(37.0, 59.0));
    vec2 starPos = starRnd - 0.5;
    vec2 d = local - starPos * (1.0 - STAR_SIZE);
    float distNorm = length(d) / max(STAR_SIZE, 0.001);

    float starSeed = hash12(cell + starRnd + vec2(float(sampleIndex) * 0.0001));
    float twinkle = 0.8 + 0.2 * sin(uTime * 6.5 + starSeed * 80.0 + float((pixel.x + pixel.y) & 255u) * 0.035);
    float hue = fract(starSeed * 1.7 + clusterA * 0.23);

    float core = smoothstep(STAR_CORE_SIZE, 0.0, distNorm);
    float glow = smoothstep(1.0, STAR_CORE_SIZE, distNorm);
    float angle = atan(d.y, d.x);
    vec3 glowColor = getStarGlowColor(clamp(distNorm, 0.0, 1.0), angle, hue);

    vec3 coreColor = hsv2rgb(vec3(hue, 0.18, 1.0));
    vec3 stars = clusterMask * twinkle * (coreColor * core * 1.8 + glowColor * glow * 1.2);

    // Nebula layers inspired by Shadertoy sample style
    vec2 nebUv = warpedUv * vec2(5.0, 2.8) + vec2(flight * 0.0007, -flight * 0.0002);
    float n0 = noise2D(nebUv);
    float n1 = noise2D(nebUv * 2.3 + vec2(0.17, 0.53));
    float n2 = noise2D(nebUv * 4.7 + vec2(0.63, 0.21));
    float neb = pow(max(0.0, n0 * 0.55 + n1 * 0.30 + n2 * 0.15), 2.1);
    vec3 nebula = hsv2rgb(vec3(fract(warpedUv.x + warpedUv.y * 0.21 + flight * 0.00012), 0.85, neb * 0.34));

    // Optional mild forward streak to suggest flight
    float streak = pow(max(0.0, 1.0 - abs(local.x + local.y * 0.2) * 2.4), 8.0) * clusterMask * 0.08;
    stars += vec3(streak);

    // Black-hole core darkening
    float bhCore = (bhPresence > 0.5) ? smoothstep(0.18, 0.0, bhDist) : 0.0;

    vec3 baseSpace = vec3(0.003, 0.004, 0.010);
    vec3 col = baseSpace + nebula + stars + tunnelCol * 0.85;
    col *= (1.0 - bhCore);
    return col;
}

vec3 skyColor(vec3 direction) {
    return starfieldColor(direction, uvec2(0u), 0u);
}

// ============================================================================
// Path Tracing
// ============================================================================

vec3 traceRay(Ray ray, uvec2 pixel, uint sampleIndex) {
    vec3 color = vec3(1.0);

    for (uint bounce = 0; bounce < MAX_BOUNCES; bounce++) {
        HitRecord hit;
        HitRecord sphereHit;
        HitRecord planeHit;
        bool hasSphereHit = hitWorld(ray, EPSILON, uCamera.far, sphereHit);
        bool hasPlaneHit = planeIntersect(ray, EPSILON, uCamera.far, planeHit);

        if (hasSphereHit && hasPlaneHit) {
            hit = (sphereHit.t < planeHit.t) ? sphereHit : planeHit;
        } else if (hasSphereHit) {
            hit = sphereHit;
        } else if (hasPlaneHit) {
            hit = planeHit;
        }

        if (hasSphereHit || hasPlaneHit) {
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
            color *= starfieldColor(ray.direction, pixel, sampleIndex);
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


