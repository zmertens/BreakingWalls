// screen.fp – Post-process fragment shader.
// Applied to the full-screen quad after the main render pass.
// Features: scanlines, vignette, chromatic aberration (subtle), colour grade.

varying mediump vec2 var_texcoord0;

uniform lowp    sampler2D texture_sampler;
uniform mediump vec4 post_params;   // x=scanline_strength, y=vignette, z=time, w=aberration
uniform mediump vec4 color_grade;   // (r_mult, g_mult, b_mult, brightness)

void main()
{
    vec2 uv = var_texcoord0;

    // ── Chromatic aberration ─────────────────────────────────────────────
    float aberr  = post_params.w * 0.003;
    vec2  offset = (uv - vec2(0.5)) * aberr;
    float r_ch   = texture2D(texture_sampler, uv - offset).r;
    float g_ch   = texture2D(texture_sampler, uv).g;
    float b_ch   = texture2D(texture_sampler, uv + offset).b;
    float alpha  = texture2D(texture_sampler, uv).a;
    vec4  col    = vec4(r_ch, g_ch, b_ch, alpha);

    // ── Scanlines ───────────────────────────────────────────────────────
    float scan_str = post_params.x;
    float scan_v   = sin(uv.y * 720.0 + post_params.z * 0.5) * 0.5 + 0.5;
    col.rgb -= scan_str * (1.0 - scan_v) * 0.25;

    // ── Vignette ────────────────────────────────────────────────────────
    float vig_str  = post_params.y;
    float dist     = length(uv - vec2(0.5));
    float vignette = smoothstep(0.85, 0.4, dist);
    col.rgb       *= mix(1.0, vignette, vig_str);

    // ── Synthwave colour grade ───────────────────────────────────────────
    col.r *= color_grade.r;
    col.g *= color_grade.g;
    col.b *= color_grade.b;
    col.rgb *= color_grade.w;   // brightness

    gl_FragColor = col;
}
