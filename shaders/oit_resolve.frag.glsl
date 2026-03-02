<<<<<<< HEAD
#version 430 core

in vec2 vTexCoord;

layout (location = 0) out vec4 FragColor;

layout (binding = 0) uniform sampler2D uOITAccumTex;
layout (binding = 1) uniform sampler2D uOITRevealTex;
=======
#version 460 core

in vec2 vTexCoord;

layout(location = 0) out vec4 FragColor;

uniform sampler2D uOITAccumTex;
uniform sampler2D uOITRevealTex;
>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d

void main()
{
    vec4 accum = texture(uOITAccumTex, vTexCoord);
<<<<<<< HEAD
    float reveal = clamp(texture(uOITRevealTex, vTexCoord).r, 0.0, 1.0);

    vec3 color = accum.rgb / max(accum.a, 1e-5);
    float alpha = 1.0 - reveal;

    FragColor = vec4(color, alpha);
}
=======
    float reveal = texture(uOITRevealTex, vTexCoord).r;
    
    // Standard OIT weighted average blending
    // accum.rgb is the accumulated color*weight, accum.a is the accumulated weight
    // reveal is the transparency (1 - alpha product)
    
    vec3 averageColor = accum.a < 1e-5 ? vec3(0.0) : (accum.rgb / accum.a);
    float alpha = 1.0 - reveal;
    
    FragColor = vec4(averageColor, alpha);
}

>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d
