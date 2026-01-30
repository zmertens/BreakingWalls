#version 430 core

in vec2 vTexCoord;

layout (location = 0) out vec4 FragColor;

layout (binding = 0) uniform sampler2D uTexture2D;

void main()
{
	vec3 color = texture(uTexture2D, vTexCoord).rgb;

	// Gamma correction is already applied in the compute shader
	// so we just pass through the color
	FragColor = vec4(color, 1.0);
}

