#version 300 es
precision highp float;

uniform float seconds;
uniform float fall_progress;
uniform samplerCube environment_map;
uniform float environment_strength;
uniform vec3 environment_center;
uniform float environment_extent;
in vec2 fragment_uv;
in vec3 world_position;
in vec3 surface_normal;
in float fragment_light;

// Balls cast soft shadows and bounce light by material: 0 diffuse, 1 metal, 2 dielectric.
uniform int ball_count;
uniform vec4 spheres[16];
uniform vec4 surfaces[16];

float ball_blocks(vec3 origin, vec3 direction, vec4 sphere) {
    vec3 offset = origin - sphere.xyz;
    float half_b = dot(offset, direction);
    float discriminant = half_b * half_b - dot(offset, offset) + sphere.w * sphere.w;
    return (discriminant >= 0.0 && half_b < 0.0) ? 1.0 : 0.0;
}

void ball_shading(vec3 point, vec3 normal, out vec3 shade, out vec3 glow) {
    shade = vec3(1.0);
    glow = vec3(0.0);
    for (int i = 0; i < 16; ++i) {
        if (i >= ball_count) break;
        vec4 sphere = spheres[i];
        // Shadow offset grows with the drop from the ball (light slope ~0.65 incl. softening) and
        // glow reaches 2.2 cells, so distant balls are skipped before any ray tests.
        vec2 flat_offset = sphere.xz - point.xz;
        float reach_limit = max(2.2, (sphere.y - point.y) * 0.65 + sphere.w + 0.1);
        if (dot(flat_offset, flat_offset) > reach_limit * reach_limit) continue;
        vec3 tint = surfaces[i].rgb;
        float kind = surfaces[i].a;
        vec3 origin = point + normal * 0.01;
        float blocked = 0.0;
        for (int s = 0; s < 4; ++s) {
            vec2 jitter = vec2((s & 1) == 0 ? -0.12 : 0.12, (s & 2) == 0 ? -0.12 : 0.12);
            blocked += ball_blocks(origin, normalize(vec3(-0.5 + jitter.x, 1.0, -0.35 + jitter.y)), sphere);
        }
        blocked *= 0.25;
        vec3 umbra = kind < 0.5 ? vec3(0.38) : (kind < 1.5 ? vec3(0.22) : mix(vec3(0.62), tint, 0.45));
        shade *= mix(vec3(1.0), umbra, blocked);
        vec3 to_ball = sphere.xyz - point;
        float distance_to_ball = length(to_ball);
        float reach = clamp(1.0 - distance_to_ball / 2.2, 0.0, 1.0);
        float facing = max(dot(normal, to_ball / max(distance_to_ball, 0.001)), 0.0);
        float bounce = kind < 0.5 ? 0.22 : (kind < 1.5 ? 0.12 : 0.34);
        glow += tint * reach * reach * facing * bounce;
    }
}

layout(location = 0) out vec4 accumulation;
layout(location = 1) out vec4 opticalDepth;

vec3 environment_direction(vec3 ray) {
    vec3 local = world_position - environment_center;
    vec3 safe_ray = mix(vec3(-1.0), vec3(1.0), step(vec3(0.0), ray)) * max(abs(ray), vec3(0.0001));
    vec3 distance_to_box = (sign(safe_ray) * environment_extent - local) / safe_ray;
    float travel = min(distance_to_box.x, min(distance_to_box.y, distance_to_box.z));
    return local + ray * travel;
}

void main() {
    vec2 local = abs(fract(world_position.xz + 0.5) - 0.5);
    float rim = smoothstep(0.40, 0.48, max(local.x, local.y));
    float depth = clamp((7.0 - world_position.y) * 0.5, 0.0, 2.0);
    float shimmer = 0.5 + 0.5 * sin(world_position.x * 2.0 + world_position.z * 1.3 + seconds * 0.6);
    float alpha = mix(0.10, 0.32, rim) + 0.03 * abs(surface_normal.x + surface_normal.z);
    vec3 color = mix(vec3(0.045, 0.20, 0.55), vec3(0.12, 0.58, 0.95), rim);
    color += vec3(0.95, 0.75, 0.15) * fragment_light;
    color *= (1.0 - depth * 0.14) * (0.92 + shimmer * 0.08);
    if (environment_strength > 0.0) {
        vec3 incident = vec3(0.0, -1.0, 0.0);
        vec3 normal = faceforward(normalize(surface_normal), incident, normalize(surface_normal));
        float fresnel = 0.04 + 0.96 * pow(1.0 - abs(dot(normal, -incident)), 5.0);
        vec3 reflected = texture(environment_map, environment_direction(reflect(incident, normal))).rgb;
        vec3 refracted = texture(environment_map, environment_direction(refract(incident, normal, 1.0 / 1.45))).rgb;
        color = mix(color, mix(refracted, reflected, fresnel), environment_strength);
    }
    vec3 ball_shade, ball_glow;
    ball_shading(world_position, vec3(0.0, 1.0, 0.0), ball_shade, ball_glow);
    float shadow_depth = 1.0 - min(ball_shade.r, min(ball_shade.g, ball_shade.b));
    color = color * ball_shade + ball_glow;
    alpha = min(alpha + shadow_depth * 0.2, 0.9);
    if (fragment_light > 4.5) {
        // Swarm bee: soft glowing dot (normal.x = tint, normal.y = swarm strength), shadowed like the board.
        float r2 = dot(fragment_uv, fragment_uv);
        if (r2 > 1.0) discard;
        float core = 1.0 - r2;
        vec3 bee = mix(vec3(1.0, 0.8, 0.15), vec3(1.0, 0.45, 0.1), surface_normal.x);
        color = bee * (0.8 + 0.5 * core * core) * ball_shade + ball_glow * 0.5;
        alpha = core * core * 0.28 * surface_normal.y;
    } else if (fragment_light > 2.5) {
        // Swipe trace: dominant color, feathered edge, shaded by the same balls as the board.
        bool active_stroke = fragment_light < 3.5;
        float feather = 1.0 - smoothstep(0.35, 1.0, abs(fragment_uv.x));
        float pulse = 0.88 + 0.12 * sin(seconds * 6.0);
        vec3 stroke = active_stroke ? vec3(1.0, 0.86, 0.22) * pulse : vec3(0.95, 0.55, 0.16);
        color = stroke * ball_shade + ball_glow * 0.5;
        alpha = (active_stroke ? 0.9 : 0.65) * feather;
    } else if (fragment_light > 1.5) {
        color = vec3(0.95, 0.65, 0.22);
        alpha = 0.55 * (1.0 - fall_progress);
    }
    accumulation = vec4(color * alpha, alpha) / 8.0;
    opticalDepth = vec4(vec3(-log(1.0 - alpha) / 16.0), 0.0);
}
