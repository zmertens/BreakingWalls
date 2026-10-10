#version 300 es
precision highp float;

out vec4 FragColor;

uniform sampler2D sampler;
uniform float timer;

in vec2 fragment_uv;
in vec3 sky_direction;

const float pi = 3.14159265;

vec3 vertical_gradient(vec3 horizon, vec3 zenith, float height)
{
    return mix(horizon, zenith, smoothstep(0.28, 0.96, height));
}

void main()
{
    float day = smoothstep(0.24, 0.42, timer) * (1.0 - smoothstep(0.78, 0.96, timer));
    float dawn = exp(-pow((timer - 0.29) / 0.095, 2.0));
    float dusk = exp(-pow((timer - 0.86) / 0.095, 2.0));
    float twilight = clamp(max(dawn, dusk), 0.0, 1.0);
    float height = clamp(normalize(sky_direction).y * 0.5 + 0.5, 0.0, 1.0);

    vec3 night = vertical_gradient(vec3(0.035, 0.055, 0.12), vec3(0.012, 0.022, 0.075), height);
    vec3 daylight = vertical_gradient(vec3(0.68, 0.79, 0.91), vec3(0.16, 0.40, 0.78), height);
    vec3 sunset = vertical_gradient(vec3(1.0, 0.31, 0.13), vec3(0.23, 0.16, 0.42), height);
    vec3 sky = mix(night, daylight, day);
    sky = mix(sky, sunset, twilight * 0.92);

    vec3 texture_tint = texture(sampler, vec2(timer, fragment_uv.t)).rgb;
    sky = mix(sky, texture_tint, 0.12);

    float phase = 2.0 * pi * (timer - 0.6);
    vec3 sun_direction = normalize(vec3(sin(phase), cos(phase), -0.24));
    vec3 moon_direction = normalize(vec3(-sin(phase), -cos(phase), -0.24));
    vec3 view_direction = normalize(sky_direction);

    float sun_visibility = smoothstep(-0.08, 0.08, sun_direction.y);
    float sun_alignment = max(dot(view_direction, sun_direction), 0.0);
    float sun_halo = pow(sun_alignment, 42.0) + 0.22 * pow(sun_alignment, 9.0);
    float sun_disc = smoothstep(0.996, 0.999, sun_alignment);
    sky += vec3(1.0, 0.36, 0.12) * sun_halo * sun_visibility * (0.7 + twilight);
    sky = mix(sky, vec3(1.0, 0.91, 0.68), sun_disc * sun_visibility);

    float moon_visibility = smoothstep(-0.08, 0.08, moon_direction.y);
    float moon_alignment = max(dot(view_direction, moon_direction), 0.0);
    float moon_halo = 0.34 * pow(moon_alignment, 38.0) + 0.08 * pow(moon_alignment, 8.0);
    float moon_disc = smoothstep(0.997, 0.9994, moon_alignment);
    sky += vec3(0.30, 0.47, 0.95) * moon_halo * moon_visibility;
    sky = mix(sky, vec3(0.78, 0.86, 1.0), moon_disc * moon_visibility);

    FragColor = vec4(sky, 1.0);
}
