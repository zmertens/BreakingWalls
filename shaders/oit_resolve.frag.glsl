#version 460 core

in vec2 vTexCoord;

layout(location = 0) out vec4 FragColor;

uniform sampler2D uOITAccumTex;
uniform sampler2D uOITRevealTex;

void main()
{
    vec4 accum = texture(uOITAccumTex, vTexCoord);
    float reveal = texture(uOITRevealTex, vTexCoord).r;
    
    // Standard OIT weighted average blending
    // accum.rgb is the accumulated color*weight, accum.a is the accumulated weight
    // reveal is the transparency (1 - alpha product)
    
    vec3 averageColor = accum.a < 1e-5 ? vec3(0.0) : (accum.rgb / accum.a);
    float alpha = 1.0 - reveal;
    
    FragColor = vec4(averageColor, alpha);
}

