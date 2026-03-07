#version 430 core

// Geometry shader to project billboard sprite shadows onto the spherical terrain.

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

// Billboard sprite setup
uniform vec3 uSpritePos;           // Billboard center position (world space)
uniform float uSpriteHalfSize;     // Half-size used when rendering the billboard
uniform vec3 uLightDir;            // Direction from sun toward the scene
uniform float uGroundY;            // Ground plane height (legacy, kept for compatibility)
uniform vec3 uSphereCenter;        // Sphere center (world space)
uniform float uSphereRadius;       // Sphere radius
uniform mat4 uViewMatrix;          // Camera view matrix
uniform mat4 uProjectionMatrix;    // Camera projection matrix
uniform vec2 uInvResolution;       // 1.0 / screen resolution

out vec2 vShadowCoord;

const float EPSILON = 1e-3;

// Project a world position onto the sphere surface along a ray direction
vec3 projectToSphereSurface(vec3 rayOrigin, vec3 rayDir, vec3 sphereCenter, float sphereRadius)
{
    // Ray-sphere intersection: find closest intersection point
    // Ray: P = rayOrigin + t * rayDir
    // Sphere: |P - sphereCenter|^2 = sphereRadius^2
    
    vec3 oc = rayOrigin - sphereCenter;
    float a = dot(rayDir, rayDir);
    float b = 2.0 * dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    
    float discriminant = b * b - 4.0 * a * c;
    
    if (discriminant < 0.0)
    {
        // No intersection, fall back to ground plane projection
        float dirY = rayDir.y;
        if (abs(dirY) < EPSILON)
        {
            dirY = (dirY >= 0.0) ? EPSILON : -EPSILON;
        }
        float t = (uGroundY - rayOrigin.y) / dirY;
        return rayOrigin + rayDir * t;
    }
    
    // Use the closer intersection point (smaller t)
    float t1 = (-b - sqrt(discriminant)) / (2.0 * a);
    float t2 = (-b + sqrt(discriminant)) / (2.0 * a);
    
    float t = (t1 > EPSILON) ? t1 : t2;
    
    if (t < EPSILON)
    {
        // Fallback to ground plane if both intersections are behind the ray origin
        float dirY = rayDir.y;
        if (abs(dirY) < EPSILON)
        {
            dirY = (dirY >= 0.0) ? EPSILON : -EPSILON;
        }
        float tGround = (uGroundY - rayOrigin.y) / dirY;
        return rayOrigin + rayDir * tGround;
    }
    
    return rayOrigin + rayDir * t;
}

vec3 projectToGround(vec3 worldPos, vec3 lightDir, float groundY)
{
    // Use sphere projection instead of flat plane
    return projectToSphereSurface(worldPos, lightDir, uSphereCenter, uSphereRadius);
}

// Snap a point onto the sphere surface (with a small bias to avoid z-fighting)
vec3 snapToSphere(vec3 pos, vec3 sphereCenter, float sphereRadius)
{
    vec3 dir = pos - sphereCenter;
    float len = length(dir);
    if (len < EPSILON)
    {
        return sphereCenter + vec3(0.0, sphereRadius, 0.0);
    }
    return sphereCenter + (dir / len) * (sphereRadius + 0.02);
}

void main()
{
    // Get camera right/up axes in world space from inverse view matrix.
    mat3 invView = mat3(inverse(uViewMatrix));
    vec3 camRight = normalize(invView[0]);
    vec3 camUp = normalize(invView[1]);

    // Estimate billboard feet point in world space.
    vec3 feetPos = uSpritePos - camUp * (uSpriteHalfSize * 0.95);

    vec3 lightDir = normalize(uLightDir);
    vec3 projectedCenter = projectToGround(feetPos, lightDir, uGroundY);

    // Compute the sphere surface normal at the projected center
    vec3 surfaceNormal = normalize(projectedCenter - uSphereCenter);

    // Project the light direction onto the tangent plane of the sphere at the projected center
    vec3 lightOnTangent = lightDir - dot(lightDir, surfaceNormal) * surfaceNormal;
    float lightOnTangentLen = length(lightOnTangent);
    vec3 tangentForward = (lightOnTangentLen > 1e-4) ? (lightOnTangent / lightOnTangentLen) : normalize(cross(surfaceNormal, vec3(0.0, 0.0, 1.0)));
    vec3 tangentSide = normalize(cross(surfaceNormal, tangentForward));

    float invLightY = 1.0 / max(abs(lightDir.y), 0.08);
    float shadowWidth = uSpriteHalfSize * 0.55;
    float shadowLength = uSpriteHalfSize * (0.90 + 0.65 * invLightY);

    // Offset corners in the sphere's tangent plane, then snap onto the sphere surface
    vec3 topLeftWorld    = snapToSphere(projectedCenter + tangentSide * shadowWidth + tangentForward * shadowLength, uSphereCenter, uSphereRadius);
    vec3 topRightWorld   = snapToSphere(projectedCenter - tangentSide * shadowWidth + tangentForward * shadowLength, uSphereCenter, uSphereRadius);
    vec3 bottomLeftWorld = snapToSphere(projectedCenter + tangentSide * shadowWidth - tangentForward * shadowLength, uSphereCenter, uSphereRadius);
    vec3 bottomRightWorld= snapToSphere(projectedCenter - tangentSide * shadowWidth - tangentForward * shadowLength, uSphereCenter, uSphereRadius);

    // Top-left
    gl_Position = uProjectionMatrix * uViewMatrix * vec4(topLeftWorld, 1.0);
    vShadowCoord = vec2(0.0, 1.0);
    EmitVertex();

    // Top-right
    gl_Position = uProjectionMatrix * uViewMatrix * vec4(topRightWorld, 1.0);
    vShadowCoord = vec2(1.0, 1.0);
    EmitVertex();

    // Bottom-left
    gl_Position = uProjectionMatrix * uViewMatrix * vec4(bottomLeftWorld, 1.0);
    vShadowCoord = vec2(0.0, 0.0);
    EmitVertex();

    // Bottom-right
    gl_Position = uProjectionMatrix * uViewMatrix * vec4(bottomRightWorld, 1.0);
    vShadowCoord = vec2(1.0, 0.0);
    EmitVertex();

    EndPrimitive();
}
