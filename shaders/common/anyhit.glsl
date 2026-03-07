/*
 * MIT License
 *
 * Copyright(c) 2019 Asif Ali
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

bool AnyHit(Ray r, float maxDist)
{
    // Intersect BVH and tris
#ifdef OPT_NO_BVH
    // Brute-force: loop over all mesh instances and their triangles
    for (int inst = 0; inst < numMeshInstances; inst++)
    {
        vec4 meshInfo = texelFetch(meshInstanceInfoTex, ivec2(inst, 0), 0);
        int triStart = int(meshInfo.x);
        int triCount = int(meshInfo.y);

        vec4 r1 = texelFetch(transformsTex, ivec2(int(meshInfo.w) * 4 + 0, 0), 0);
        vec4 r2 = texelFetch(transformsTex, ivec2(int(meshInfo.w) * 4 + 1, 0), 0);
        vec4 r3 = texelFetch(transformsTex, ivec2(int(meshInfo.w) * 4 + 2, 0), 0);
        vec4 r4 = texelFetch(transformsTex, ivec2(int(meshInfo.w) * 4 + 3, 0), 0);
        mat4 xfm = mat4(r1, r2, r3, r4);

        vec3 rOrg = (inverse(xfm) * vec4(r.origin, 1.0)).xyz;
        vec3 rDir = (inverse(xfm) * vec4(r.direction, 0.0)).xyz;

        for (int i = triStart; i < triStart + triCount; i++)
        {
            ivec3 vertIndices = ivec3(texelFetch(vertexIndicesTex, i).xyz);

            vec4 v0 = texelFetch(verticesTex, vertIndices.x);
            vec4 v1 = texelFetch(verticesTex, vertIndices.y);
            vec4 v2 = texelFetch(verticesTex, vertIndices.z);

            vec3 e0 = v1.xyz - v0.xyz;
            vec3 e1 = v2.xyz - v0.xyz;
            vec3 pv = cross(rDir, e1);
            float det = dot(e0, pv);

            // Two-sided Moller-Trumbore test (robust to triangle winding)
            if (abs(det) < 1e-8)
                continue;

            float invDet = 1.0 / det;

            vec3 tv = rOrg - v0.xyz;
            float u = dot(tv, pv) * invDet;
            if (u < 0.0 || u > 1.0)
                continue;

            vec3 qv = cross(tv, e0);
            float v = dot(rDir, qv) * invDet;
            if (v < 0.0 || (u + v) > 1.0)
                continue;

            float tHit = dot(e1, qv) * invDet;
            if (tHit > EPS && tHit < maxDist)
                return true;
        }
    }

    return false;
#else
    int stack[64];
    int ptr = 0;
    stack[ptr++] = -1;

    int index = topBVHIndex;
    float leftHit = 0.0;
    float rightHit = 0.0;

#if defined(OPT_ALPHA_TEST) && !defined(OPT_MEDIUM)
    int currMatID = 0;
#endif
    bool BLAS = false;

    Ray rTrans;
    rTrans.origin = r.origin;
    rTrans.direction = r.direction;

    while (index != -1)
    {
        ivec3 LRLeaf = ivec3(texelFetch(BVH, index * 3 + 2).xyz);

        int leftIndex  = int(LRLeaf.x);
        int rightIndex = int(LRLeaf.y);
        int leaf       = int(LRLeaf.z);

        if (leaf > 0) // Leaf node of BLAS
        {
            for (int i = 0; i < rightIndex; i++) // Loop through tris
            {
                ivec3 vertIndices = ivec3(texelFetch(vertexIndicesTex, leftIndex + i).xyz);

                vec4 v0 = texelFetch(verticesTex, vertIndices.x);
                vec4 v1 = texelFetch(verticesTex, vertIndices.y);
                vec4 v2 = texelFetch(verticesTex, vertIndices.z);

                vec3 e0 = v1.xyz - v0.xyz;
                vec3 e1 = v2.xyz - v0.xyz;
                vec3 pv = cross(rTrans.direction, e1);
                float det = dot(e0, pv);

                vec3 tv = rTrans.origin - v0.xyz;
                vec3 qv = cross(tv, e0);

                vec4 uvt;
                uvt.x = dot(tv, pv);
                uvt.y = dot(rTrans.direction, qv);
                uvt.z = dot(e1, qv);
                uvt.xyz = uvt.xyz / det;
                uvt.w = 1.0 - uvt.x - uvt.y;

                if (all(greaterThanEqual(uvt, vec4(0.0))) && uvt.z < maxDist)
                {
#if defined(OPT_ALPHA_TEST) && !defined(OPT_MEDIUM)
                    vec2 t0 = vec2(v0.w, texelFetch(normalsTex, vertIndices.x).w);
                    vec2 t1 = vec2(v1.w, texelFetch(normalsTex, vertIndices.y).w);
                    vec2 t2 = vec2(v2.w, texelFetch(normalsTex, vertIndices.z).w);

                    vec2 texCoord = t0 * uvt.w + t1 * uvt.x + t2 * uvt.y;

                    vec4 texIDs      = texelFetch(materialsTex, ivec2(currMatID * 8 + 6, 0), 0);
                    vec4 alphaParams = texelFetch(materialsTex, ivec2(currMatID * 8 + 7, 0), 0);
                    
                    float alpha = texture(textureMapsArrayTex, vec3(texCoord, texIDs.x)).a;

                    float opacity = alphaParams.x;
                    int alphaMode = int(alphaParams.y);
                    float alphaCutoff = alphaParams.z;
                    opacity *= alpha;

                    // Ignore intersection and continue ray based on alpha test
                    if (!((alphaMode == ALPHA_MODE_MASK && opacity < alphaCutoff) || 
                          (alphaMode == ALPHA_MODE_BLEND && rand() > opacity)))
                        return true;
#else
                    return true;
#endif
                }
                    
            }
        }
        else if (leaf < 0) // Leaf node of TLAS
        {
            vec4 r1 = texelFetch(transformsTex, ivec2((-leaf - 1) * 4 + 0, 0), 0).xyzw;
            vec4 r2 = texelFetch(transformsTex, ivec2((-leaf - 1) * 4 + 1, 0), 0).xyzw;
            vec4 r3 = texelFetch(transformsTex, ivec2((-leaf - 1) * 4 + 2, 0), 0).xyzw;
            vec4 r4 = texelFetch(transformsTex, ivec2((-leaf - 1) * 4 + 3, 0), 0).xyzw;

            mat4 transform = mat4(r1, r2, r3, r4);

            rTrans.origin    = vec3(inverse(transform) * vec4(r.origin, 1.0));
            rTrans.direction = vec3(inverse(transform) * vec4(r.direction, 0.0));

            // Add a marker. We'll return to this spot after we've traversed the entire BLAS
            stack[ptr++] = -1;

            index = leftIndex;
            BLAS = true;
#if defined(OPT_ALPHA_TEST) && !defined(OPT_MEDIUM)
            currMatID = rightIndex;
#endif
            continue;
        }
        else
        {
            leftHit =  AABBIntersect(texelFetch(BVH, leftIndex  * 3 + 0).xyz, texelFetch(BVH, leftIndex  * 3 + 1).xyz, rTrans);
            rightHit = AABBIntersect(texelFetch(BVH, rightIndex * 3 + 0).xyz, texelFetch(BVH, rightIndex * 3 + 1).xyz, rTrans);

            if (leftHit > 0.0 && rightHit > 0.0)
            {
                int deferred = -1;
                if (leftHit > rightHit)
                {
                    index = rightIndex;
                    deferred = leftIndex;
                }
                else
                {
                    index = leftIndex;
                    deferred = rightIndex;
                }

                stack[ptr++] = deferred;
                continue;
            }
            else if (leftHit > 0.)
            {
                index = leftIndex;
                continue;
            }
            else if (rightHit > 0.)
            {
                index = rightIndex;
                continue;
            }
        }
        index = stack[--ptr];

        // If we've traversed the entire BLAS then switch to back to TLAS and resume where we left off
        if (BLAS && index == -1)
        {
            BLAS = false;

            index = stack[--ptr];

            rTrans.origin = r.origin;
            rTrans.direction = r.direction;
        }
    }

    return false;
#endif // OPT_NO_BVH
}