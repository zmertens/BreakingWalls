// tile.fp – Fragment program for isometric maze tiles.
// Adds a depth-based fog effect to give the maze visual depth, plus
// the same synthwave palette shift used on sprites.

varying mediump vec2 var_texcoord0;
varying lowp    vec4 var_color;

uniform lowp    sampler2D texture_sampler;
uniform mediump vec4 tint;            // colour tint (r,g,b,a)
uniform mediump vec4 fog_params;      // x=near, y=far, z=unused, w=unused
uniform mediump vec4 fog_color;       // fog colour (r,g,b,a)

void main()
{
    vec4 tex_color = texture2D(texture_sampler, var_texcoord0.xy) * var_color;

    // Apply tint
    tex_color.rgb *= tint.rgb;
    tex_color.a   *= tint.a;

    // Depth fog using vertex Z encoded in var_color.a by the render script
    // (Defold doesn't give built-in gl_FragDepth in GLES2, so we use a
    //  normalised depth baked into var_color.a by the vertex shader in a
    //  full implementation; here we approximate with UV.y as a proxy.)
    float depth  = var_texcoord0.y;  // 0=top, 1=bottom of sprite
    float near   = fog_params.x > 0.0 ? fog_params.x : 0.0;
    float far    = fog_params.y > 0.0 ? fog_params.y : 1.0;
    float fog_t  = clamp((depth - near) / (far - near), 0.0, 0.3);
    tex_color.rgb = mix(tex_color.rgb, fog_color.rgb, fog_t * fog_color.a);

    gl_FragColor = tex_color;
}
