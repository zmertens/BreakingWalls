// sprite.fp – Fragment program for animated sprites.
// Applies a synthwave colour tint, subtle scanline overlay,
// and a vignette effect — all scalable via uniforms.

varying mediump vec2 var_texcoord0;
varying lowp    vec4 var_color;

uniform lowp    sampler2D texture_sampler;

// Shader constants (set from material or script)
uniform mediump vec4 tint;           // (r,g,b,a)  default (1,1,1,1)
uniform mediump vec4 scanline_params; // x=strength (0..1), y=density, z=time, w=unused
uniform mediump vec4 vignette_params; // x=strength (0..1), y=radius,  z=softness, w=unused

void main()
{
    vec4 tex_color = texture2D(texture_sampler, var_texcoord0.xy) * var_color;

    // ── Synthwave tint ──────────────────────────────────────────────────
    tex_color.rgb *= tint.rgb;
    tex_color.a   *= tint.a;

    // ── Scanlines ───────────────────────────────────────────────────────
    // Horizontal scanline pattern in screen-space via UV.y
    float scan_density  = scanline_params.y > 0.0 ? scanline_params.y : 200.0;
    float scan_strength = scanline_params.x;
    float scan_wave     = sin(var_texcoord0.y * scan_density + scanline_params.z) * 0.5 + 0.5;
    float scan_dim      = 1.0 - scan_strength * (1.0 - scan_wave) * 0.4;
    tex_color.rgb *= scan_dim;

    // ── Vignette ────────────────────────────────────────────────────────
    float vig_strength = vignette_params.x;
    float vig_radius   = vignette_params.y > 0.0 ? vignette_params.y : 0.75;
    float vig_softness = vignette_params.z > 0.0 ? vignette_params.z : 0.45;
    vec2  uv_center    = var_texcoord0 - vec2(0.5, 0.5);
    float dist         = length(uv_center);
    float vignette     = smoothstep(vig_radius, vig_radius - vig_softness, dist);
    tex_color.rgb     *= mix(1.0, vignette, vig_strength);

    gl_FragColor = tex_color;
}
