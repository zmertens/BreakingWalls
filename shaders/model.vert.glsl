#version 430 core

layout (location = 0) in vec3 Position;
layout (location = 1) in vec2 TexCoord;
layout (location = 2) in vec3 Normal;
layout (location = 3) in ivec4 BoneIDs;
layout (location = 4) in vec4 BoneWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uBones[200];
uniform uint uBoneCount;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vTexCoord;

void main()
{
    mat4 skin = mat4(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        int boneId = BoneIDs[i];
        float weight = BoneWeights[i];
        if (weight <= 0.0)
        {
            continue;
        }

        if (boneId >= 0 && uint(boneId) < uBoneCount)
        {
            skin += uBones[boneId] * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight <= 1e-6)
    {
        skin = mat4(1.0);
    }
    else
    {
        skin *= (1.0 / totalWeight);
    }

    vec4 localPos = skin * vec4(Position, 1.0);
    vec3 localNormal = normalize((skin * vec4(Normal, 0.0)).xyz);

    vec4 worldPos = uModel * localPos;
    mat3 normalMat = mat3(transpose(inverse(uModel)));
    vWorldNormal = normalize(normalMat * localNormal);
    vWorldPos = worldPos.xyz;
    vTexCoord = TexCoord;

    gl_Position = uProjection * uView * worldPos;
}
