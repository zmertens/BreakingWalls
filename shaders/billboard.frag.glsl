#version 430 core

in vec2 TexCoord;

uniform sampler2D SpriteTex;

layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 texColor = texture(SpriteTex, TexCoord);
    
    // Alpha testing - discard transparent pixels
    if (texColor.a < 0.1)
        discard;
    
    FragColor = texColor;
}
