#version 450 core

// Path tracing compute shader with progressive refinement
// Based on physically-based rendering principles

#define MAX_SPHERES 200  // Increased for dynamic spawning
#define MAX_LIGHTS 5
#define MAX_BOUNCES 8
#define EPSILON 0.001

// Starfield parameters
#define PI 3.14159265359
#define TWO_PI 6.28318530718

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

// Shadow casting uniforms
uniform vec3 uPlayerPos;          // Player position in world space
uniform float uPlayerRadius;      // Player shadow radius
uniform vec3 uLightDir;           // Light direction (normalized)

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
// Shadow Ray Casting Functions
// ============================================================================

bool testPlayerShadow(in Ray shadowRay, float maxDist) {
    // Test if shadow ray intersects player sphere
    vec3 oc = uPlayerPos - shadowRay.origin;
    float a = dot(shadowRay.direction, shadowRay.direction);
    float halfB = dot(oc, shadowRay.direction);
    float c = dot(oc, oc) - uPlayerRadius * uPlayerRadius;
    
    float discriminant = halfB * halfB - a * c;
    if (discriminant < 0.0) return false;
    
    float sqrtD = sqrt(discriminant);
    float root = (-halfB - sqrtD) / a;
    
    // Check if intersection is within valid range
    return root > EPSILON && root < maxDist;
}

bool testSpheresShadow(in Ray shadowRay, float maxDist) {
    // Test if shadow ray intersects any sphere
    for (uint i = 0u; i < uSphereCount; i++) {
        vec3 oc = shadowRay.origin - bSpheres[i].center.xyz;
        float a = dot(shadowRay.direction, shadowRay.direction);
        float halfB = dot(oc, shadowRay.direction);
        float c = dot(oc, oc) - bSpheres[i].radius2;
        
        float discriminant = halfB * halfB - a * c;
        if (discriminant < 0.0) continue;
        
        float sqrtD = sqrt(discriminant);
        float root = (-halfB - sqrtD) / a;
        
        if (root > EPSILON && root < maxDist) {
            return true;
        }
    }
    return false;
}

bool testPlaneShadow(in Ray shadowRay, float maxDist) {
    // Test if shadow ray intersects ground plane
    vec3 planeNormal = normalize(uGroundPlaneNormal);
    float denom = dot(planeNormal, shadowRay.direction);
    
    if (abs(denom) < EPSILON) return false;
    
    float t = dot(uGroundPlanePoint - shadowRay.origin, planeNormal) / denom;
    return t > EPSILON && t < maxDist;
}

float calculateShadow(vec3 hitPoint, vec3 normal) {
    // Create shadow ray from hit point away from light (opposite of light direction)
    vec3 shadowRayOrigin = hitPoint + normal * 0.01;  // Slightly offset to avoid self-intersection
    Ray shadowRay = Ray(shadowRayOrigin, -uLightDir);
    
    // Check if hit point is a significant distance from player (to avoid shadowing player itself)
    float distToPlayer = length(hitPoint - uPlayerPos);
    float minDistForPlayerShadow = uPlayerRadius * 0.5;  // Don't shadow very close points
    
    // Maximum distance for shadow ray
    float maxDist = uCamera.far;
    
    float shadowFactor = 1.0;  // Start fully lit
    
    // Check occlusion by player - with soft penumbra using noise
    if (distToPlayer > minDistForPlayerShadow) {
        if (testPlayerShadow(shadowRay, maxDist)) {
            // Calculate shadow softness based on distance from player
            float shadowFalloff = smoothstep(0.0, uPlayerRadius * 2.0, distToPlayer);
            
            // Sample noise texture for soft shadow edges
            vec2 shadowNoiseUV = hitPoint.xz * 0.3 + vec2(uTime * 0.5);
            float shadowNoise = texture(uNoiseTex, shadowNoiseUV).r;
            
            // Blend between hard shadow and soft shadow edge using noise
            float hardShadow = 0.15;  // Very dark core shadow
            float softShadow = mix(0.4, 0.85, shadowNoise);  // Jittered edge
            
            shadowFactor = mix(hardShadow, softShadow, shadowFalloff * shadowNoise);
        }
    }
    
    // Check occlusion by other spheres
    if (testSpheresShadow(shadowRay, maxDist)) {
        float sphereShadow = mix(0.35, 0.7, texture(uNoiseTex, hitPoint.xy * 0.2).r);
        shadowFactor = min(shadowFactor, sphereShadow);
    }
    
    // Ground plane adds subtle soft shadow with noise
    if (testPlaneShadow(shadowRay, maxDist)) {
        // Very subtle shadow with noise jitter for ground
        float groundNoise = texture(uNoiseTex, hitPoint.xz * 0.1).r;
        float groundShadow = mix(0.88, 0.92, groundNoise);
        shadowFactor = min(shadowFactor, groundShadow);
    }
    
    return shadowFactor;
}

// ============================================================================
// Grid Function for Ground Plane
// ============================================================================

float grid(vec2 uv, float perspective) {
    vec2 size = vec2(uv.y, uv.y * uv.y * 0.2) * 0.01;
    uv += vec2(0.0, uTime * 4.0 * (perspective + 0.05));
    uv = abs(fract(uv) - 0.5);
    vec2 lines = smoothstep(size, vec2(0.0), uv);
    lines += smoothstep(size * 5.0, vec2(0.0), uv) * 0.4 * perspective;
    return clamp(lines.x + lines.y, 0.0, 3.0);
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

float hash21(vec2 co) {
    return fract(sin(dot(co.xy,vec2(1.9898,7.233)))*45758.5433);
}

// Star noise function - creates a clean starfield effect
float starnoise(vec3 rd) {
    float c = 0.0;
    vec3 p = normalize(rd) * 300.0;
    for (float i = 0.0; i < 4.0; i++) {
        vec3 q = fract(p) - 0.5;
        vec3 id = floor(p);
        float c2 = smoothstep(0.5, 0.0, length(q));
        c2 *= step(hash21(id.xz/id.y), 0.06 - i*i*0.005);
        c += c2;
        p = p*0.6 + 0.5*p*mat3(3.0/5.0, 0.0, 4.0/5.0, 0.0, 1.0, 0.0, -4.0/5.0, 0.0, 3.0/5.0);
    }
    c *= c;
    float g = dot(sin(rd*10.512), cos(rd.yzx*10.512));
    c *= smoothstep(-3.14, -0.9, g)*0.5 + 0.5*smoothstep(-0.3, 1.0, g);
    return c*c;
}

// Add sun/sunset effect at the center
void addSun(vec3 rd, vec3 sunDir, inout vec3 col) {
    float sun = smoothstep(0.21, 0.2, distance(rd, sunDir));
    
    if (sun > 0.0) {
        float yd = (rd.y - sunDir.y);
        float a = sin(3.1 * exp(-(yd) * 14.0));
        sun *= smoothstep(-0.8, 0.0, a);
        col = mix(col, vec3(1.0, 0.8, 0.4) * 0.75, sun);
    }
}

vec3 starfieldColor(vec3 direction, uvec2 pixel, uint sampleIndex) {
    vec3 rd = normalize(direction);
    
    // Create sun direction - pointing down towards horizon for sunset at +X
    vec3 sunDir = normalize(vec3(1.0, -0.125 + 0.05*sin(0.1*uTime), 0.0));
    
    // Horizon distance for sunset effect (rd.y = 0 is horizon)
    float horizonDist = abs(rd.y);
    float sunsetBlend = smoothstep(0.4, -0.2, rd.y);  // Strong effect near and below horizon
    
    // Sky gradient base - purple at top
    vec3 skyTop = vec3(0.2, 0.1, 0.4);
    
    // Sunset colors - warm orange/red at horizon
    vec3 sunsetBot = vec3(1.0, 0.5, 0.2);  // Golden orange
    vec3 sunsetMid = vec3(0.8, 0.2, 0.4);  // Red-magenta
    
    // Blend sky color based on vertical position
    vec3 sky = mix(skyTop, sunsetMid, sunsetBlend * 0.7);
    sky = mix(sky, sunsetBot, sunsetBlend);
    
    // Add atmospheric haze/fog effect using noise texture
    float noiseFog = texture(uNoiseTex, vec2(0.5 + 0.05*rd.x/max(0.1, rd.z), 0.0)).x;
    float atmosphericHaze = mix(0.1, 1.0, sunsetBlend) * (0.7 + 0.3 * noiseFog);
    
    // Stars - reduce visibility near horizon to show sunset
    float starVisibility = smoothstep(-0.1, 0.3, rd.y);  // Fade stars at horizon
    float st = starnoise(rd) * starVisibility;
    
    // Combine with proper layering
    vec3 col = mix(sky, vec3(st), 0.6 * starVisibility);
    
    // Enhance glow at sun location
    addSun(rd, sunDir, col);
    
    return clamp(col, 0.0, 1.0);
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
        bool hitIsPlane = false;

        if (hasSphereHit && hasPlaneHit) {
            if (sphereHit.t < planeHit.t) {
                hit = sphereHit;
            } else {
                hit = planeHit;
                hitIsPlane = true;
            }
        } else if (hasSphereHit) {
            hit = sphereHit;
        } else if (hasPlaneHit) {
            hit = planeHit;
            hitIsPlane = true;
        }

        if (hasSphereHit || hasPlaneHit) {
            vec3 attenuation;
            Ray scattered;

            if (scatter(hit, ray, attenuation, scattered, pixel, sampleIndex, bounce)) {
                // Calculate shadow factor
                float shadowFactor = calculateShadow(hit.point, hit.normal);
                
                // Apply grid overlay to ground plane
                if (hitIsPlane) {
                    // Use hit point XZ coordinates for grid pattern
                    vec2 gridUV = hit.point.xz * 0.3;  // Scale factor for grid size
                    float gridVal = grid(gridUV, 0.8);
                    
                    // Blend grid with ground plane color - more prominent
                    vec3 gridColor = vec3(0.0, 1.0, 1.0);  // Cyan grid color
                    attenuation = mix(attenuation, gridColor, gridVal * 0.6);
                }
                
                // Apply shadow to attenuation
                attenuation *= shadowFactor;
                
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


