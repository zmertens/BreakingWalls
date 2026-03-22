#version 430 core

uniform mat4 uMVP;
uniform vec3 uTileCenter;
uniform vec2 uTileHalfSize;

out vec2 vLocal;

void main()
{
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0)
    );

    vec2 local = corners[gl_VertexID];
    vec3 worldPos = vec3(uTileCenter.x + local.x * uTileHalfSize.x,
                         uTileCenter.y,
                         uTileCenter.z + local.y * uTileHalfSize.y);
    gl_Position = uMVP * vec4(worldPos, 1.0);
    vLocal = local;
}
