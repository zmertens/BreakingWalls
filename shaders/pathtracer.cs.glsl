#version 450 core

// Path tracing compute shader with progressive refinement
// Based on physically-based rendering principles

#define MAX_SPHERES 200  // Increased for dynamic spawning
#define MAX_TRIANGLES 2048
#define MAX_TRIANGLE_SHADOW_TESTS 96
#define MAX_LIGHTS 5
#define MAX_BOUNCES 8
#define EPSILON 0.001
#define INV_PI 0.31830988618
#define THROUGHPUT_LUMA_CLAMP 6.0
#define DIRECT_LUMA_CLAMP 8.0
#define SAMPLE_LUMA_CLAMP 12.0

// Starfield parameters
#define PI 3.14159265359
#define TWO_PI 6.28318530718

// Material types (must match C++ MaterialType enum)
#define LAMBERTIAN 0
#define METAL 1
#define DIELECTRIC 2

// ============================================================================

// ...existing code...
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

vec3 randomCosineHemisphere(vec3 normal, uvec2 pixel, uint sampleIndex, uint bounce) {
    vec2 xi = random2(pixel, sampleIndex, bounce * 17u + 3u);
    float phi = TWO_PI * xi.x;
    float r = sqrt(xi.y);
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0, 1.0 - xi.y));

    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * x + bitangent * y + normal * z);
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 clampByLuminance(vec3 c, float maxLuma) {
    float y = luminance(c);
    if (y > maxLuma && y > EPSILON) {
        c *= (maxLuma / y);
    }
    return c;
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

struct Triangle {
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 n0;
    vec4 n1;
    vec4 n2;
    vec4 albedoAndMaterial;
    vec4 materialParams;
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
uniform uint uPrimaryRaySphereCount;  // Sphere count for primary rays (exclude reflection-only proxy)
uniform uint uTriangleCount;

uniform float uTime;
uniform float uHistoryBlend;
uniform sampler2D uNoiseTex;
uniform vec3 uPlayerPos; // Player position in world space
uniform uint uVoronoiCellCount;

// Shadow casting uniforms
uniform float uPlayerRadius;      // Player shadow radius
uniform vec3 uLightDir;           // Light direction (normalized)

// Shader storage buffer for spheres
layout (std430, binding = 1) buffer SphereBuffer {
    Sphere bSpheres[MAX_SPHERES];
};

layout (std430, binding = 2) buffer TriangleBuffer {
    Triangle bTriangles[MAX_TRIANGLES];
};

layout (std430, binding = 3) readonly buffer VoronoiCellColorBuffer {
    vec4 bVoronoiCellColors[];
};

layout (std430, binding = 4) readonly buffer VoronoiSeedBuffer {
    vec4 bVoronoiSeeds[];
};

layout (std430, binding = 5) readonly buffer VoronoiPaintedBuffer {
    uint bVoronoiPainted[];
};

// ============================================================================
// Intersection Functions
// Plane intersection (Y=0 ground plane)
bool planeIntersect(in Ray ray, out float t, out vec3 hitPoint, out vec3 normal) {
    // Plane: y = 0
    if (abs(ray.direction.y) < EPSILON) return false;
    t = -ray.origin.y / ray.direction.y;
    if (t < EPSILON) return false;
    hitPoint = ray.origin + t * ray.direction;
    normal = vec3(0.0, 1.0, 0.0);
    return true;
}

// Voronoi coloring for ground plane
vec3 voronoiColorAt(vec3 p) {
    // Project to XZ plane and wrap for infinite tiling
    float tileSize = 100.0; // Should match seed generation range
    vec2 pos = mod(p.xz + tileSize * 1000.0, tileSize); // Offset to avoid negative mod issues
    float minDist = 1e20;
    uint cellIdx = 0u;
    for (uint i = 0u; i < uVoronoiCellCount; ++i) {
        vec2 seed = mod(bVoronoiSeeds[i].xz + tileSize * 1000.0, tileSize);
        float d = distance(pos, seed);
        if (d < minDist) { minDist = d; cellIdx = i; }
    }
    return bVoronoiCellColors[cellIdx].rgb;
}
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

bool hitWorld(in Ray ray, float tMin, float tMax, uint sphereCount, out HitRecord hit) {
    HitRecord tempHit;
    bool hitAnything = false;
    float closest = tMax;

    for (uint i = 0; i < sphereCount; i++) {
        if (sphereIntersect(bSpheres[i], ray, tMin, closest, tempHit)) {
            hitAnything = true;
            closest = tempHit.t;
            hit = tempHit;
        }
    }

    return hitAnything;
}



bool triangleIntersect(in Triangle triangle, in Ray ray, float tMin, float tMax, out HitRecord hit) {
    vec3 v0 = triangle.v0.xyz;
    vec3 v1 = triangle.v1.xyz;
    vec3 v2 = triangle.v2.xyz;

    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;

    vec3 pvec = cross(ray.direction, edge2);
    float det = dot(edge1, pvec);
    if (abs(det) < EPSILON) {
        return false;
    }

    float invDet = 1.0 / det;
    vec3 tvec = ray.origin - v0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) {
        return false;
    }

    vec3 qvec = cross(tvec, edge1);
    float v = dot(ray.direction, qvec) * invDet;
    if (v < 0.0 || (u + v) > 1.0) {
        return false;
    }

    float t = dot(edge2, qvec) * invDet;
    if (t < tMin || t > tMax) {
        return false;
    }

    float w = 1.0 - u - v;
    vec3 interpNormal = triangle.n0.xyz * w + triangle.n1.xyz * u + triangle.n2.xyz * v;
    if (length(interpNormal) < EPSILON) {
        interpNormal = cross(edge1, edge2);
    }
    interpNormal = normalize(interpNormal);

    hit.t = t;
    hit.point = ray.origin + ray.direction * t;
    hit.frontFace = dot(ray.direction, interpNormal) < 0.0;
    hit.normal = hit.frontFace ? interpNormal : -interpNormal;
    hit.materialType = uint(triangle.albedoAndMaterial.w + 0.5);
    hit.albedo = triangle.albedoAndMaterial.rgb;
    hit.fuzz = triangle.materialParams.x;
    hit.refractiveIndex = triangle.materialParams.y;

    return true;
}

bool hitTriangles(in Ray ray, float tMin, float tMax, uint triangleCount, out HitRecord hit) {
    HitRecord tempHit;
    bool hitAnything = false;
    float closest = tMax;

    uint count = min(triangleCount, uint(MAX_TRIANGLES));
    for (uint i = 0u; i < count; i++) {
        if (triangleIntersect(bTriangles[i], ray, tMin, closest, tempHit)) {
            hitAnything = true;
            closest = tempHit.t;
            hit = tempHit;
        }
    }

    return hitAnything;
}

// ============================================================================
// Shadow Ray Casting Functions
// ============================================================================

bool testPlayerShadow(in Ray shadowRay, float maxDist) {
    // Test if shadow ray intersects player sphere
    vec3 oc = shadowRay.origin - uPlayerPos;
    float a = dot(shadowRay.direction, shadowRay.direction);
    float halfB = dot(oc, shadowRay.direction);
    float c = dot(oc, oc) - uPlayerRadius * uPlayerRadius;
    
    float discriminant = halfB * halfB - a * c;
    if (discriminant < 0.0) return false;
    
    float sqrtD = sqrt(discriminant);
    float root = (-halfB - sqrtD) / a;
    if (root > EPSILON && root < maxDist) {
        return true;
    }

    // When the ray starts inside the sphere, the near root is negative;
    // test the far root so nearby contact shadows remain stable.
    root = (-halfB + sqrtD) / a;
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

        root = (-halfB + sqrtD) / a;
        if (root > EPSILON && root < maxDist) {
            return true;
        }
    }
    return false;
}



bool testTrianglesShadow(in Ray shadowRay, float maxDist) {
    uint total = min(uTriangleCount, uint(MAX_TRIANGLES));
    uint count = min(total, uint(MAX_TRIANGLE_SHADOW_TESTS));
    if (count == 0u) {
        return false;
    }

    // Sample across the full triangle range instead of only the first N entries
    // so large meshes still cast coherent shadows.
    uint stride = max(1u, total / count);
    for (uint s = 0u; s < count; s++) {
        uint i = min(s * stride, total - 1u);
        HitRecord tmp;
        if (triangleIntersect(bTriangles[i], shadowRay, EPSILON, maxDist, tmp)) {
            return true;
        }
    }
    return false;
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

    if (testTrianglesShadow(shadowRay, maxDist)) {
        float triShadow = mix(0.30, 0.65, texture(uNoiseTex, hitPoint.zy * 0.17).r);
        shadowFactor = min(shadowFactor, triShadow);
    }
    

    
    return shadowFactor;
}

float visibilityToSun(vec3 hitPoint, vec3 normal) {
    vec3 lightDir = normalize(-uLightDir);
    Ray shadowRay = Ray(hitPoint + normal * 0.02, lightDir);
    float maxDist = uCamera.far;

    if (testPlayerShadow(shadowRay, maxDist)) return 0.0;
    if (testSpheresShadow(shadowRay, maxDist)) return 0.0;
    if (testTrianglesShadow(shadowRay, maxDist)) return 0.0;

    return 1.0;
}

vec3 estimateDirectSun(in HitRecord hit) {
    if (hit.materialType != LAMBERTIAN) {
        return vec3(0.0);
    }

    vec3 lightDir = normalize(-uLightDir);
    float nDotL = max(dot(hit.normal, lightDir), 0.0);
    if (nDotL <= 0.0) {
        return vec3(0.0);
    }

    float visibility = visibilityToSun(hit.point, hit.normal);
    if (visibility <= 0.0) {
        return vec3(0.0);
    }

    vec3 sunRadiance = vec3(2.5, 2.3, 2.1);
    vec3 lambertBRDF = hit.albedo * INV_PI;
    float bsdfPdf = nDotL * INV_PI;
    float neeWeight = 1.0 / (1.0 + bsdfPdf);
    return lambertBRDF * sunRadiance * nDotL * visibility * neeWeight;
}

// ============================================================================

vec3 sampleVoronoiGroundColor(vec3 point, out bool hasVoronoiData) {
    hasVoronoiData = false;

    uint count = uVoronoiCellCount;
    if (count == 0u) {
        return vec3(0.0);
    }

    float bestDist2 = 1e30;
    uint bestIndex = 0u;
    vec2 pointXZ = point.xz;

    for (uint i = 0u; i < count; ++i) {
        vec2 seedXZ = bVoronoiSeeds[i].xz;
        vec2 delta = pointXZ - seedXZ;
        float dist2 = dot(delta, delta);
        if (dist2 < bestDist2) {
            bestDist2 = dist2;
            bestIndex = i;
        }
    }

    if (bVoronoiPainted[bestIndex] == 0u) {
        return vec3(0.0);
    }

    hasVoronoiData = true;
    return bVoronoiCellColors[bestIndex].rgb;
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

vec3 fresnelSchlickVec(float cosTheta, vec3 F0) {
    return F0 + (vec3(1.0) - F0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

vec3 sampleGGXHalfVector(vec3 normal, float roughness, uvec2 pixel, uint sampleIndex, uint bounce) {
    vec2 xi = random2(pixel, sampleIndex, bounce * 29u + 11u);

    float alpha = max(0.02, roughness * roughness);
    float alpha2 = alpha * alpha;

    float phi = TWO_PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha2 - 1.0) * xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    vec3 hLocal = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * hLocal.x + bitangent * hLocal.y + normal * hLocal.z);
}

// Scatter ray based on material type
bool scatter(in HitRecord hit, in Ray rayIn, out vec3 attenuation, out Ray scattered,
             uvec2 pixel, uint sampleIndex, uint bounce) {

    // LAMBERTIAN (Diffuse)
    if (hit.materialType == LAMBERTIAN) {
        vec3 scatterDir = randomCosineHemisphere(hit.normal, pixel, sampleIndex, bounce);

        scattered = Ray(hit.point, normalize(scatterDir));
        attenuation = hit.albedo;
        return true;
    }

    // METAL (Reflective)
    if (hit.materialType == METAL) {
        vec3 V = normalize(-rayIn.direction);
        float roughness = clamp(hit.fuzz, 0.02, 1.0);
        vec3 H = sampleGGXHalfVector(hit.normal, roughness, pixel, sampleIndex, bounce);
        if (dot(V, H) <= 0.0) {
            H = -H;
        }

        vec3 L = normalize(reflect(rayIn.direction, H));
        if (dot(L, hit.normal) <= EPSILON) {
            return false;
        }

        vec3 F0 = clamp(hit.albedo, vec3(0.04), vec3(0.98));
        attenuation = fresnelSchlickVec(max(dot(L, H), 0.0), F0);
        vec3 offsetNormal = dot(L, hit.normal) > 0.0 ? hit.normal : -hit.normal;
        scattered = Ray(hit.point + offsetNormal * 0.01, L);
        return true;
    }

    // DIELECTRIC (Glass)
    if (hit.materialType == DIELECTRIC) {
        vec3 V = normalize(-rayIn.direction);
        float roughness = clamp(hit.fuzz, 0.0, 1.0);
        vec3 H = sampleGGXHalfVector(hit.normal, roughness, pixel, sampleIndex, bounce);
        if (dot(V, H) <= 0.0) {
            H = -H;
        }

        float etaI = hit.frontFace ? 1.0 : hit.refractiveIndex;
        float etaT = hit.frontFace ? hit.refractiveIndex : 1.0;
        float eta = etaI / etaT;

        float F0s = ((etaT - etaI) / (etaT + etaI));
        F0s *= F0s;
        float cosVH = max(dot(V, H), 0.0);
        float reflectProb = F0s + (1.0 - F0s) * pow(1.0 - cosVH, 5.0);

        vec3 direction;
        if (random(pixel, sampleIndex, bounce * 131u + 73u) < reflectProb) {
            direction = reflect(rayIn.direction, H);
        } else {
            direction = refract(rayIn.direction, H, eta);
            if (length(direction) < EPSILON) {
                direction = reflect(rayIn.direction, H);
            }
        }

        attenuation = vec3(1.0);
        vec3 offsetNormal = dot(direction, hit.normal) > 0.0 ? hit.normal : -hit.normal;
        scattered = Ray(hit.point + offsetNormal * 0.01, normalize(direction));
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
    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);


    for (uint bounce = 0; bounce < MAX_BOUNCES; bounce++) {
        HitRecord hit;
        HitRecord sphereHit;
        HitRecord triangleHit;
        float planeT;
        vec3 planeHitPoint, planeNormal;
        uint sphereCountForBounce = (bounce == 0u) ? uPrimaryRaySphereCount : uSphereCount;
        bool hasSphereHit = hitWorld(ray, EPSILON, uCamera.far, sphereCountForBounce, sphereHit);
        uint triangleCountForBounce = (bounce == 0u)
            ? uTriangleCount
            : ((bounce == 1u) ? min(uTriangleCount, 32u) : 0u);
        bool hasTriangleHit = hitTriangles(ray, EPSILON, uCamera.far, triangleCountForBounce, triangleHit);
        bool hasPlaneHit = planeIntersect(ray, planeT, planeHitPoint, planeNormal);

        // Find closest hit
        float closestT = uCamera.far;
        int hitType = -1; // 0 = plane, 1 = sphere, 2 = triangle

        if (hasPlaneHit && planeT < closestT) {
            closestT = planeT;
            hitType = 0;
        }
        if (hasSphereHit && sphereHit.t < closestT) {
            closestT = sphereHit.t;
            hitType = 1;
        }
        if (hasTriangleHit && triangleHit.t < closestT) {
            closestT = triangleHit.t;
            hitType = 2;
        }

        if (hitType == 0) {
            // Shade Voronoi ground plane, taking painted state into account
            vec3 cellColor = sampleVoronoiGroundColor(planeHitPoint);
            radiance += throughput * cellColor;
            return clampByLuminance(radiance, SAMPLE_LUMA_CLAMP);
        } else if (hitType == 1) {
            hit = sphereHit;
        } else if (hitType == 2) {
            hit = triangleHit;
        } else {
            // Hit sky
            radiance += throughput * starfieldColor(ray.direction, pixel, sampleIndex);
            return clampByLuminance(radiance, SAMPLE_LUMA_CLAMP);
        }

        // Standard material shading for spheres/triangles
        vec3 attenuation;
        Ray scattered;
        vec3 direct = throughput * estimateDirectSun(hit);
        direct = clampByLuminance(direct, DIRECT_LUMA_CLAMP);
        radiance += direct;

        if (scatter(hit, ray, attenuation, scattered, pixel, sampleIndex, bounce)) {
            throughput *= attenuation;
            throughput = clampByLuminance(throughput, THROUGHPUT_LUMA_CLAMP);
            ray = scattered;
        } else {
            // Absorbed
            return clampByLuminance(radiance, SAMPLE_LUMA_CLAMP);
        }

        // Russian roulette termination for performance
        if (bounce > 3) {
            float p = max(throughput.r, max(throughput.g, throughput.b));
            p = clamp(p, 0.05, 0.99);
            if (random(pixel, sampleIndex, bounce * 1000u) > p) {
                return clampByLuminance(radiance, SAMPLE_LUMA_CLAMP);
            }
            throughput /= p;
            throughput = clampByLuminance(throughput, THROUGHPUT_LUMA_CLAMP);
        }
    }

    return clampByLuminance(radiance, SAMPLE_LUMA_CLAMP); // Exceeded max bounces
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
        color = clampByLuminance(color, SAMPLE_LUMA_CLAMP);
        accumulatedColor += color;
    }

    // Per-frame sample average
    vec3 batchColor = accumulatedColor / float(max(1u, uSamplesPerBatch));

    // Smooth temporal accumulation (EMA-style) for natural, non-stuttering motion blur
    vec3 historyColor = batchColor;
    if (uBatch > 0u) {
        vec3 previous = imageLoad(uAccumTexture, ivec2(pixel)).rgb;
        float historyBlend = clamp(uHistoryBlend, 0.0, 0.999);
        historyColor = mix(batchColor, previous, historyBlend);
    }

    imageStore(uAccumTexture, ivec2(pixel), vec4(historyColor, 1.0));

    // Gamma correction (gamma = 2.0)
    vec3 averageColor = sqrt(historyColor);

    imageStore(uDisplayTexture, ivec2(pixel), vec4(averageColor, 1.0));
}


