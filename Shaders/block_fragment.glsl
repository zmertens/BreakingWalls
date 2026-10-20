#version 330 core

uniform sampler2D sampler;
uniform float timer;
uniform float daylight;
uniform bool is_ortho;
uniform float bloom_emission;
uniform float light_attenuation;
uniform bool mirror_mask;
uniform samplerCube environment_map;
uniform float environment_strength;

in vec3 world_position;
in vec3 surface_normal;
in vec3 view_direction;
in vec2 fragment_uv;
in float fragment_ao;
in float fragment_light;
in float fog_factor;
in float fog_height;
in float diffuse;

layout (location = 0) out vec4 fragColor;
layout (location = 1) out vec4 brightColor;


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

const float pi = 3.14159265;

void main() {
    vec4 texColor = texture(sampler, fragment_uv);
    vec3 color = texColor.rgb;

    // Discard transparent pixels or magenta color key FIRST (before any other checks)
    // Relaxed thresholds to handle texture filtering/mipmapping artifacts
    bool is_magenta = (color.r > 0.7 && color.g < 0.3 && color.b > 0.7 &&
                      abs(color.r - color.b) < 0.3 && color.g < min(color.r, color.b) * 0.5);
    if (texColor.a < 0.5 || is_magenta) {
        discard;
    }
    // Stencil mask pass: only the upward-facing tops of the top layer receive reflections.
    if (mirror_mask && (normalize(surface_normal).y < 0.9 || world_position.y < 7.0)) {
        discard;
    }

    // Detect maze texture: fragment_light == 1.0 AND fragment_ao == 1.0 (from uv.z=0.0)
    // This must come after transparency check but before cloud check
    // Render it without any lighting, AO, or fog effects
    if (fragment_light >= 0.99 && fragment_ao >= 0.99) {
        // Maze texture detected - render at full brightness without lighting/fog
        fragColor = vec4(color, 1.0);
        brightColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }


    bool cloud = color == vec3(1.0, 1.0, 1.0);
    if (cloud && is_ortho) {
        discard;
    }
    float df = cloud ? diffuse * 1.2 : diffuse;
    float ao = cloud ? fragment_ao * 1.2 : fragment_ao;

    float local_light = fragment_light * (1.0 - clamp(light_attenuation, 0.0, 1.0));
    ao += local_light;
    df += local_light;
    float value = daylight + local_light;
    vec3 light_color = vec3(value * 0.3 + 0.2);
    vec3 ambient = vec3(value * 0.3 + 0.2);
    vec3 light = ambient + light_color * df;
    color *= light * ao;
    vec3 ball_shade, ball_glow;
    ball_shading(world_position, normalize(surface_normal), ball_shade, ball_glow);
    color = color * ball_shade + ball_glow;
    if (environment_strength > 0.0) {
        vec3 incident = normalize(-view_direction);
        vec3 normal = faceforward(normalize(surface_normal), incident, normalize(surface_normal));
        float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(normal, -incident), 0.0), 5.0);
        vec3 reflected = texture(environment_map, reflect(incident, normal)).rgb;
        vec3 refracted = texture(environment_map, refract(incident, normal, 1.0 / 1.33)).rgb;
        vec3 environment = mix(refracted, reflected, fresnel) * 1.4;
        color = mix(color, environment, clamp(environment_strength * (0.35 + 0.65 * fresnel), 0.0, 1.0));
    }
    // Apply fog with sky blue color that matches the background
    vec3 fog_color = vec3(0.53, 0.81, 0.92) * (daylight * 0.4 + 0.6);
    color = mix(color, fog_color, fog_factor);
    fragColor = vec4(color, 1.0);

    float brightness = dot(fragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if (bloom_emission > 0.0) {
        brightColor = vec4(color * fragment_light * bloom_emission, 1.0);
    } else if (brightness > 1.0) {
        brightColor = vec4(fragColor.rgb, 1.0);
    } else {
        brightColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
