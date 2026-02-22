#version 430 core

// Geometry shader to project billboard sprite shadows onto the ground plane.

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

// Billboard sprite setup
uniform vec3 uSpritePos;           // Billboard center position (world space)
uniform float uSpriteHalfSize;     // Half-size used when rendering the billboard
uniform vec3 uLightDir;            // Direction from sun toward the scene
uniform float uGroundY;            // Ground plane height
uniform mat4 uViewMatrix;          // Camera view matrix
uniform mat4 uProjectionMatrix;    // Camera projection matrix
uniform vec2 uInvResolution;       // 1.0 / screen resolution

out vec2 vShadowCoord;

const float EPSILON = 1e-3;

vec3 projectToGround(vec3 worldPos, vec3 lightDir, float groundY)
{
    float dirY = lightDir.y;
    if (abs(dirY) < EPSILON)
    {
        dirY = (dirY >= 0.0) ? EPSILON : -EPSILON;
    }

    float t = (groundY - worldPos.y) / dirY;
    return worldPos + lightDir * t;
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

    // Build a ground-plane basis: light direction projected onto XZ plus perpendicular.
    vec2 lightXZ = lightDir.xz;
    float lightXZLen = length(lightXZ);
    vec2 lightForward = (lightXZLen > 1e-4) ? (lightXZ / lightXZLen) : vec2(1.0, 0.0);
    vec2 lightSide = vec2(-lightForward.y, lightForward.x);

    float invLightY = 1.0 / max(abs(lightDir.y), 0.08);
    float shadowWidth = uSpriteHalfSize * 0.55;
    float shadowLength = uSpriteHalfSize * (0.90 + 0.65 * invLightY);

    vec3 topLeftWorld = projectedCenter + vec3(lightSide.x * shadowWidth + lightForward.x * shadowLength,
                                                0.0,
                                                lightSide.y * shadowWidth + lightForward.y * shadowLength);
    vec3 topRightWorld = projectedCenter + vec3(-lightSide.x * shadowWidth + lightForward.x * shadowLength,
                                                 0.0,
                                                 -lightSide.y * shadowWidth + lightForward.y * shadowLength);
    vec3 bottomLeftWorld = projectedCenter + vec3(lightSide.x * shadowWidth - lightForward.x * shadowLength,
                                                   0.0,
                                                   lightSide.y * shadowWidth - lightForward.y * shadowLength);
    vec3 bottomRightWorld = projectedCenter + vec3(-lightSide.x * shadowWidth - lightForward.x * shadowLength,
                                                    0.0,
                                                    -lightSide.y * shadowWidth - lightForward.y * shadowLength);

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
