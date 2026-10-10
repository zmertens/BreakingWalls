#version 300 es
precision highp float;
precision highp int;

uniform mat4 matrix;
uniform int ball_index;
uniform vec4 spheres[16];
flat out int sphere_index;
out vec3 ray_origin;
out vec3 ray_direction;

void main() {
    sphere_index = ball_index;
    vec4 sphere = spheres[ball_index];
    vec2 corner = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0,
                       (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
    vec4 center = matrix * vec4(sphere.xyz, 1.0);
    vec2 extent = sphere.w * vec2(length(vec3(matrix[0][0], matrix[1][0], matrix[2][0])),
                                 length(vec3(matrix[0][1], matrix[1][1], matrix[2][1])));
    vec2 clip = center.xy + corner * extent;
    mat4 unproject = inverse(matrix);
    vec4 near_point = unproject * vec4(clip, -1.0, 1.0);
    vec4 far_point = unproject * vec4(clip, 1.0, 1.0);
    ray_origin = near_point.xyz / near_point.w;
    ray_direction = far_point.xyz / far_point.w - ray_origin;
    gl_Position = vec4(clip, 0.0, 1.0);
}
