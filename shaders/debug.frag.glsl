#version 430 core

layout (binding = 0) uniform sampler2D uTexture;

in vec2 vTexCoord;
out vec4 outColor;

void main()
{
    vec4 texColor = texture(uTexture, vTexCoord);
    // Display with enhanced visibility for debugging
    outColor = vec4(texColor.rgb * 3.0, 1.0);
}
