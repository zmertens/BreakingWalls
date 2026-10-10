#version 330 core

uniform mat4 matrix;
uniform int ball_count;
uniform vec4 spheres[16];
uniform vec4 surfaces[16];
uniform samplerCube environment_map;
uniform bool environment_enabled;
uniform bool mirrored;
flat in int sphere_index;
in vec3 ray_origin;
in vec3 ray_direction;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 brightColor;

float hit_sphere(vec3 origin, vec3 direction, vec4 sphere) {
    vec3 offset = origin - sphere.xyz;
    float half_b = dot(offset, direction);
    float discriminant = half_b * half_b - dot(offset, offset) + sphere.w * sphere.w;
    if (discriminant < 0.0) return -1.0;
    float root = sqrt(discriminant);
    float distance = -half_b - root;
    return distance > 0.001 ? distance : (-half_b + root > 0.001 ? -half_b + root : -1.0);
}

vec3 environment(vec3 direction) {
    if (environment_enabled)
        return texture(environment_map, mirrored ? direction * vec3(1.0, -1.0, 1.0) : direction).rgb * 0.85 + vec3(0.06);
    float sky = clamp((mirrored ? -direction.y : direction.y) * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.04, 0.07, 0.16), vec3(0.55, 0.65, 0.8), sky);
}

vec3 lighting(vec3 normal, vec3 view, vec3 color, float visibility) {
    vec3 light = normalize(vec3(-0.5, mirrored ? -1.0 : 1.0, -0.35));
    float diffuse = max(dot(normal, light), 0.0);
    float specular = pow(max(dot(normal, normalize(light + view)), 0.0), 48.0);
    return color * (0.18 + diffuse * visibility * 0.82) + vec3(specular * visibility * 0.45);
}

// One secondary scene query; no recursion or full-screen primary sphere search.
vec3 secondary(vec3 origin, vec3 direction, int skip) {
    float closest = 1000.0;
    int found = -1;
    for (int i = 0; i < 16; ++i) {
        if (i >= ball_count) break;
        if (i == skip) continue;
        float distance = hit_sphere(origin, direction, spheres[i]);
        if (distance > 0.0 && distance < closest) {
            closest = distance;
            found = i;
        }
    }
    if (found < 0) return environment(direction);
    vec3 normal = normalize(origin + direction * closest - spheres[found].xyz);
    return lighting(normal, -direction, surfaces[found].rgb, 1.0);
}

float shadow(vec3 point, vec3 normal) {
    float visible = 0.0;
    for (int sample_index = 0; sample_index < 4; ++sample_index) {
        vec2 offset = vec2((sample_index & 1) == 0 ? -0.12 : 0.12,
                           (sample_index & 2) == 0 ? -0.12 : 0.12);
        vec3 light = normalize(vec3(-0.5 + offset.x, mirrored ? -1.0 : 1.0, -0.35 + offset.y));
        float blocked = 0.0;
        for (int i = 0; i < 16; ++i) {
            if (i >= ball_count) break;
            if (i == sphere_index) continue;
            if (hit_sphere(point + normal * 0.002, light, spheres[i]) > 0.0) {
                blocked = 1.0;
                break;
            }
        }
        visible += 1.0 - blocked;
    }
    return visible * 0.25;
}

void main() {
    vec3 direction = normalize(ray_direction);
    vec4 sphere = spheres[sphere_index];
    vec3 origin = ray_origin + direction * (dot(sphere.xyz - ray_origin, direction) - 2.0 * sphere.w);
    float distance = hit_sphere(origin, direction, sphere);
    if (distance < 0.0) discard;
    vec3 point = origin + distance * direction;
    vec3 normal = normalize(point - sphere.xyz);
    vec4 surface = surfaces[sphere_index];
    vec3 radiance = lighting(normal, -direction, surface.rgb, shadow(point, normal));
    vec3 reflected = reflect(direction, normal);
    if (surface.a > 0.5 && surface.a < 1.5) {
        vec3 fresnel = surface.rgb + (vec3(1.0) - surface.rgb) *
                       pow(1.0 - max(dot(-direction, normal), 0.0), 5.0);
        radiance = radiance * 0.2 + fresnel * secondary(point + normal * 0.002, reflected, sphere_index);
    } else if (surface.a >= 1.5) {
        float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(-direction, normal), 0.0), 5.0);
        vec3 inside = refract(direction, normal, 1.0 / 1.5);
        float exit_distance = hit_sphere(point - normal * 0.002, inside, sphere);
        vec3 exit_point = point - normal * 0.002 + inside * max(exit_distance, 0.0);
        vec3 exit_normal = normalize(exit_point - sphere.xyz);
        vec3 transmitted = refract(inside, -exit_normal, 1.5);
        vec3 reflection = secondary(point + normal * 0.002, reflected, sphere_index);
        if (dot(transmitted, transmitted) < 0.001) {
            radiance = reflection;
        } else {
            vec3 transmission = secondary(exit_point + exit_normal * 0.002, normalize(transmitted), sphere_index);
            radiance = mix(transmission * mix(vec3(1.0), surface.rgb, 0.2), reflection, fresnel) +
                       radiance * 0.12;
        }
    }
    vec4 clip = matrix * vec4(point, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    fragColor = vec4(radiance, 1.0);
    brightColor = vec4(max(radiance - vec3(0.85), vec3(0.0)), 1.0);
}
