#version 330 core

in vec2 vTexCoord;
in vec2 vWorldPos;

out vec4 FragColor;

uniform vec4  uColor;
uniform int   uUseTexture;
uniform sampler2D uTexture;

uniform float uTime;
uniform float uGlow;

uniform int   uStarField;

uniform int   uLightEnabled;
uniform vec2  uLightPos;
uniform float uLightRadius;
uniform float uAmbient;

float hash12(vec2 p) {
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float starField(vec2 uv, float t) {
	vec2 grid = floor(uv * 120.0);
	vec2 local = fract(uv * 120.0) - 0.5;

	float rnd = hash12(grid);
	float star = step(0.992, rnd);

	float twinkle = 0.65 + 0.35 * sin(t * 2.5 + rnd * 100.0);
	float d = length(local);
	float shape = smoothstep(0.12, 0.0, d);
	return star * shape * twinkle;
}

void main() {
	vec4 base = uColor;

	if (uUseTexture == 1) {
		base *= texture(uTexture, vTexCoord);
	}

	vec3 rgb = base.rgb;
	float alpha = base.a;

	if (uStarField == 1) {
		float stars = starField(vTexCoord, uTime);
		rgb += vec3(stars);
	}

	if (uLightEnabled == 1) {
		float distToLight = distance(vWorldPos, uLightPos);
		float atten = 1.0 - smoothstep(0.0, max(0.0001, uLightRadius), distToLight);
		float lightTerm = max(uAmbient, atten);
		rgb *= lightTerm;
	}

	if (uGlow > 0.0) {
		float edge = min(min(vTexCoord.x, 1.0 - vTexCoord.x),
						 min(vTexCoord.y, 1.0 - vTexCoord.y));
		float rim = smoothstep(0.22, 0.0, edge);
		float pulse = 0.8 + 0.2 * sin(uTime * 5.5);
		rgb += uGlow * rim * pulse * 0.5;
	}

	FragColor = vec4(clamp(rgb, 0.0, 1.0), alpha);
}
