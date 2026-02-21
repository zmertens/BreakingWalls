#version 430 core

// Geometry shader to create a screen-space shadow quad
// Renders shadow directly in screen space for simple screen-aligned shadow

layout (points) in;
layout (triangle_strip, max_vertices = 4) out;

// Player/Character setup
uniform vec3 uPlayerPos;           // Character center position (world space)
uniform float uPlayerRadius;       // Character radius
uniform mat4 uViewMatrix;          // Camera view matrix
uniform mat4 uProjectionMatrix;    // Camera projection matrix
uniform vec2 uInvResolution;       // 1.0 / screen resolution

out vec2 vShadowCoord;

void main()
{
    // Project player position to screen space
    // Offset downward to cast shadow from feet, not center/head
    vec3 feetPos = uPlayerPos - vec3(0.0, uPlayerRadius * 1.2, 0.0);  // Position at feet level
    
    vec4 playerClip = uProjectionMatrix * uViewMatrix * vec4(feetPos, 1.0);
    vec3 playerNDC = playerClip.xyz / playerClip.w;  // Normalize to [-1, 1]
    vec2 playerScreen = (playerNDC.xy + 1.0) * 0.5;  // Convert to [0, 1]
    
    // Shadow quad dimensions in screen space (fixed, readable size)
    float shadowWidth = 0.12;   // 12% of screen width on each side
    float shadowHeight = 0.08;  // 8% of screen height above/below
    
    // No additional vertical offset - feet position is already low
    float shadowOffsetY = 0.0;
    
    // Create quad corners in NDC space
    // Shadow quad positioned at feet in screen space
    
    // Top-left (-x, +y)
    vec2 topLeft = playerScreen + vec2(-shadowWidth, shadowOffsetY + shadowHeight);
    
    // Top-right (+x, +y)
    vec2 topRight = playerScreen + vec2(shadowWidth, shadowOffsetY + shadowHeight);
    
    // Bottom-left (-x, -y)
    vec2 bottomLeft = playerScreen + vec2(-shadowWidth, shadowOffsetY - shadowHeight);
    
    // Bottom-right (+x, -y)
    vec2 bottomRight = playerScreen + vec2(shadowWidth, shadowOffsetY - shadowHeight);
    
    // Emit shadow quad in screen space (convert back to NDC)
    
    // Top-left
    gl_Position = vec4(topLeft * 2.0 - 1.0, playerNDC.z, 1.0);
    vShadowCoord = vec2(0.0, 1.0);
    EmitVertex();
    
    // Top-right
    gl_Position = vec4(topRight * 2.0 - 1.0, playerNDC.z, 1.0);
    vShadowCoord = vec2(1.0, 1.0);
    EmitVertex();
    
    // Bottom-left
    gl_Position = vec4(bottomLeft * 2.0 - 1.0, playerNDC.z, 1.0);
    vShadowCoord = vec2(0.0, 0.0);
    EmitVertex();
    
    // Bottom-right
    gl_Position = vec4(bottomRight * 2.0 - 1.0, playerNDC.z, 1.0);
    vShadowCoord = vec2(1.0, 0.0);
    EmitVertex();
    
    EndPrimitive();
}
