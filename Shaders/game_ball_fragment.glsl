#version 330 core

uniform mat4 matrix;
uniform int ball_count;
uniform vec4 spheres[16];
uniform vec4 surfaces[16];
uniform samplerCube environment_map;
uniform bool environment_enabled;
uniform bool mirrored;
uniform float seconds;
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

vec3 animated_surface(vec3 normal, vec4 sphere, vec4 surface) {
    const float pi = 3.141592653589793;
    float spin = seconds * (1.8 + fract(sphere.x * 11.13 + sphere.z * 7.19) * 2.7);
    vec2 uv = vec2(atan(normal.z, normal.x) / (2.0 * pi) + 0.5,
                  0.5 + asin(normal.y) / pi);
    uv = uv * vec2(2.5, 1.35);
    mat2 rotation = mat2(cos(spin), -sin(spin), sin(spin), cos(spin));
    uv = rotation * uv;

    float bands = 0.5 + 0.5 * sin((uv.x * 16.0 + uv.y * 8.0) + spin * 4.5);
    float flecks = 0.5 + 0.5 * sin((uv.x * 28.0 - uv.y * 19.0) + spin * 6.0);
    float grain = 0.5 + 0.5 * sin((uv.x + uv.y * 1.8) * 26.0 + spin * 5.0) *
                 cos((uv.x - uv.y) * 18.0 - spin * 2.5);
    float texture_mix = smoothstep(0.2, 1.0, bands * 0.45 + flecks * 0.35 + grain * 0.2);
    vec3 base = surface.rgb;
    vec3 lit = mix(base, vec3(1.0), 0.18 + 0.42 * texture_mix);
    vec3 accent = mix(base * 1.25, vec3(0.92, 0.96, 1.0), 0.6);
    vec3 textured = mix(lit, accent, 0.18 + 0.28 * sin((uv.x * 10.0 + spin * 2.0)));

    float material = clamp(surface.a, 0.0, 2.0);
    if (material < 0.5) {
        return mix(base, textured, 0.55);
    } else if (material < 1.5) {
        vec3 metal = mix(base * 0.72, texture_mix * vec3(0.85, 0.9, 1.0), 0.7);
        return mix(metal, textured, 0.8) + vec3(0.08, 0.1, 0.12) * texture_mix;
    }
    vec3 glass = mix(base, vec3(1.0), 0.25 + 0.35 * texture_mix);
    return mix(glass, vec3(0.9, 0.96, 1.0), 0.22 * smoothstep(0.7, 1.0, texture_mix));
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
    vec3 surface_color = animated_surface(normal, sphere, surface);
    vec3 radiance = lighting(normal, -direction, surface_color, shadow(point, normal));
    vec3 reflected = reflect(direction, normal);
    if (surface.a > 0.5 && surface.a < 1.5) {
        vec3 fresnel = surface_color + (vec3(1.0) - surface_color) *
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
            radiance = mix(transmission * mix(vec3(1.0), surface_color, 0.2), reflection, fresnel) +
                       radiance * 0.12;
        }
    }
    vec4 clip = matrix * vec4(point, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    fragColor = vec4(radiance, 1.0);
    brightColor = vec4(max(radiance - vec3(0.85), vec3(0.0)), 1.0);
}
