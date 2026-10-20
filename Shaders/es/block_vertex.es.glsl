#version 300 es
precision highp float;

uniform mat4 matrix;
uniform vec3 camera;
uniform float fog_distance;
uniform bool is_ortho;
uniform float stack_taper;
uniform float stack_center;
uniform float fall_progress;

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 uv;

out vec2 fragment_uv;
out float fragment_ao;
out float fragment_light;
out float fog_factor;
out float fog_height;
out float diffuse;
out vec3 world_position;
out vec3 surface_normal;
out vec3 view_direction;

const float pi = 3.14159265;
const vec3 light_direction = normalize(vec3(-1.0, 1.0, -1.0));

void main() {
    vec4 viewed = position;
    if (uv.w > 1.5 && uv.w < 2.5 && stack_taper > 0.0)
        viewed.y -= 2.0 * fall_progress;
    float shrink = 1.0 - stack_taper * max(0.0, (7.0 - viewed.y) * 0.5);
    viewed.xz = (viewed.xz - stack_center) * shrink + stack_center;
    gl_Position = matrix * viewed;
    world_position = position.xyz;
    surface_normal = normal;
    view_direction = camera - world_position;
    fragment_uv = uv.xy;
    fragment_ao = 0.3 + (1.0 - uv.z) * 0.7;
    fragment_light = uv.w;
    diffuse = max(0.0, dot(normal, light_direction));
    if (is_ortho) {
        fog_factor = 0.0;
        fog_height = 0.0;
    }
    else {
        float camera_distance = distance(camera, vec3(position));
        fog_factor = pow(clamp(camera_distance / fog_distance, 0.0, 1.0), 4.0);
        float dy = position.y - camera.y;
        float dx = distance(position.xz, camera.xz);
        fog_height = (atan(dy, dx) + pi / 2.0) / pi;
    }
}
