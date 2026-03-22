#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in ivec4 aBoneIds;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uBones[200];
uniform uint uBoneCount;
uniform int  uHasTexCoord;

out vec3 vWorldNormal;
out vec3 vWorldPos;

void main()
{
    float wt = aBoneWeights.x + aBoneWeights.y + aBoneWeights.z + aBoneWeights.w;
    vec4 skinnedPos;
    vec3 skinnedNorm;
    if (wt > 0.001 && uBoneCount > 1u)
    {
        skinnedPos  = vec4(0.0);
        skinnedNorm = vec3(0.0);
        for (int i = 0; i < 4; ++i)
        {
            int   id = aBoneIds[i];
            float w  = aBoneWeights[i];
            if (w <= 0.0 || id < 0 || uint(id) >= uBoneCount) continue;
            skinnedPos  += w * (uBones[id] * vec4(aPosition, 1.0));
            skinnedNorm += w * (mat3(uBones[id]) * aNormal);
        }
    }
    else
    {
        skinnedPos  = vec4(aPosition, 1.0);
        skinnedNorm = aNormal;
    }
    vec4 worldPos  = uModel * skinnedPos;
    vWorldNormal = normalize(transpose(inverse(mat3(uModel))) * normalize(skinnedNorm));
    vWorldPos    = worldPos.xyz;
    gl_Position  = uProjection * uView * worldPos;
}


