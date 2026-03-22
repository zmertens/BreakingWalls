#version 430 core

in vec3 vColor;
in vec3 vWorldPos;
in vec2 vTexCoord;

uniform sampler2D uSpriteSheet;
uniform int uHasTexture;
uniform float uTime;
uniform vec2 uPlayerXZ;
uniform vec2 uMazeOriginXZ;
uniform float uCellSize;
uniform int uHighlightEnabled;

layout (location = 0) out vec4 FragColor;

void main()
{
    vec3 color = vColor;

    // Apply sprite sheet texture to walls (vertical surfaces have y > floor level)
    if (uHasTexture != 0 && vWorldPos.y > 0.1)
    {
        // Tile the sprite sheet across walls
        vec2 uv = fract(vTexCoord * 2.0);
        // Select a region of the sprite sheet (first character frame area)
        uv = uv * 0.125; // Use 1/8 of the sheet
        vec4 texColor = texture(uSpriteSheet, uv);
        color = mix(color, texColor.rgb, 0.35);
    }

    // Add reflection effect - simulate environment reflection on walls
    float reflectiveness = 0.0;
    if (vWorldPos.y > 0.1)
    {
        // Fresnel-like reflection based on view angle approximation
        float wallFacing = abs(sin(vWorldPos.x * 0.5) * cos(vWorldPos.z * 0.5));
        reflectiveness = wallFacing * 0.25;
        
        // Animated reflection shimmer (refractive caustics)
        float caustic = sin(vWorldPos.x * 3.0 + uTime * 1.5) *
                        cos(vWorldPos.z * 2.7 + uTime * 1.2) * 0.5 + 0.5;
        vec3 reflectionColor = mix(vec3(0.3, 0.4, 0.8), vec3(0.8, 0.6, 1.0), caustic);
        color = mix(color, reflectionColor, reflectiveness);
    }

    // Add refraction-like distortion on floor
    if (vWorldPos.y <= 0.1)
    {
        float refract = sin(vWorldPos.x * 5.0 + uTime * 0.8) *
                        cos(vWorldPos.z * 4.5 + uTime * 0.6) * 0.08;
        color += vec3(refract * 0.3, refract * 0.2, refract * 0.5);

        if (uHighlightEnabled != 0)
        {
            vec2 playerCell = floor((uPlayerXZ - uMazeOriginXZ) / max(uCellSize, 0.0001));
            vec2 currentCell = floor((vWorldPos.xz - uMazeOriginXZ) / max(uCellSize, 0.0001));
            vec2 tileDelta = abs(currentCell - playerCell);
            float maxDelta = max(tileDelta.x, tileDelta.y);
            vec2 localInCell = abs(fract((vWorldPos.xz - uMazeOriginXZ) / max(uCellSize, 0.0001)) - 0.5) * 2.0;
            float edge = max(localInCell.x, localInCell.y);
            float border = smoothstep(0.72, 0.96, edge);
            float pulse = 0.92 + 0.08 * sin(uTime * 4.2);

            if (maxDelta < 0.5)
            {
                vec3 fillColor = mix(vColor, vec3(0.98, 1.0, 0.98), 0.78);
                vec3 borderColor = vec3(0.40, 1.0, 0.72);
                color = mix(fillColor, borderColor, border) * pulse;
            }
            else if (maxDelta < 1.5)
            {
                vec3 fillColor = mix(vColor, vec3(0.80, 1.0, 0.86), 0.62);
                vec3 borderColor = vec3(0.22, 0.95, 0.68);
                color = mix(fillColor, borderColor, border * 0.85) * pulse;
            }
            else if (maxDelta < 2.5)
            {
                vec3 fillColor = mix(vColor, vec3(0.55, 0.92, 0.70), 0.45);
                vec3 borderColor = vec3(0.18, 0.72, 0.52);
                color = mix(fillColor, borderColor, border * 0.75) * pulse;
            }
        }
    }

    FragColor = vec4(color, 1.0);
}
