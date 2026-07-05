#ifndef COMMON_RT_HLSLI
#define COMMON_RT_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/Vertex.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/SampleGenerators.hlsli"

#ifndef MAX_BOUNCES
#   define MAX_BOUNCES (2)
#endif

#ifndef MAX_SAMPLES
#   define MAX_SAMPLES (1)
#endif

#define SHADOW_MAX_DEPTH (1)

#define DIFFUSE_RAY_HITGROUP_IDX 0
#define DIFFUSE_RAY_MISS_IDX 0

#define SHADOW_RAY_HITGROUP_IDX 1
#define SHADOW_RAY_MISS_IDX 1

#define RAY_TMAX (1e10f)
#define SHADOW_RAY_TMAX (1e5f)

#define GN_BIAS_MAX (0.5f)

#define MIN_DIFFUSE_SHADOW (0.0001f)
#define MIN_RADIANCE (0.01f)

float3 GetView(uint2 idx, uint2 size, float4x4 projInverse)
{
    const float2 uv = float2(idx + 0.5f) / size;
    
    float2 screenPos = uv * 2.0f - 1.0f;
    screenPos.y = -screenPos.y;

    const float4 clip = float4(screenPos, 1.0f, 1.0f);
    float4 viewDirection = mul(projInverse, clip);
   
    return viewDirection.xyz / viewDirection.w;
}

RayDesc SetupPrimaryRay(float3 viewDirection, CameraData camera)
{
    RayDesc ray;
    ray.Origin = camera.Position.xyz;
    ray.Direction = normalize(mul((float3x3)camera.ViewInverse, viewDirection.xyz));
    ray.TMin = 0.1f;
    ray.TMax = RAY_TMAX;
    
    return ray;
}

RayDesc SetupPrimaryRay(uint2 idx, uint2 size, CameraData camera)
{
    float3 viewDirection = GetView(idx, size, camera.ProjInverse);

    RayDesc ray;
    ray.Origin = camera.Position.xyz;
    ray.Direction = normalize(mul((float3x3)camera.ViewInverse, viewDirection.xyz));
    ray.TMin = 0.1f;
    ray.TMax = RAY_TMAX;
    
    return ray;
}


uint InitRandomSeed(uint2 coord, uint2 size, uint frameCount)
{
    uint seed = Hash32(frameCount + kRandomFrameSalt);
    seed = Hash32Combine(seed, (coord.x << 16) | (coord.y & 0xFFFFu));
    seed = Hash32Combine(seed, size.x);
    seed = Hash32Combine(seed, size.y);
    return seed;
}

uint InitRandomSeed(uint2 coord, uint2 size, uint frameCount, uint vertexIndex, uint effectSeed)
{
    uint seed = InitRandomSeed(coord, size, frameCount);
    seed = Hash32Combine(seed, vertexIndex);
    seed = Hash32Combine(seed, effectSeed);
    return seed;
}

uint PCGHash(uint seed)
{
    return Hash32(seed + kRandomStepSalt);
}

float Random(inout uint seed)
{
    seed = Hash32(seed + kRandomStepSalt);
    return Hash32ToFloat(seed);
}

float ComputeRayConeTriangleLODValue(in Vertex v0, in Vertex v1, in Vertex v2, float3x3 world)
{
    float3 vertexPositions[3];
    vertexPositions[0] = v0.Position;
    vertexPositions[1] = v1.Position;
    vertexPositions[2] = v2.Position;

    float2 vertexTexcoords[3];
    vertexTexcoords[0] = v0.Texcoord0;
    vertexTexcoords[1] = v1.Texcoord0;
    vertexTexcoords[2] = v2.Texcoord0;

    return computeRayConeTriangleLODValue(
        vertexPositions,
        vertexTexcoords,
        world
    );
}

float3 SampleConeUniform(inout uint randomSeed, in float cosMax)
{
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);
    float phi = 2.0f * K_PI * r1;

    float cosTheta = 1.0f - r2 * (1.0f - cosMax);
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
}

float3 SampleCosineHemisphere(float2 sample)
{
    float r = sqrt(sample.x);
    float theta = 2.0 * K_PI * sample.y;

    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(1.0 - sample.x);

    return float3(x, y, z);
}

float3 SampleCosineHemisphere(inout uint seed)
{
    return SampleCosineHemisphere(float2(Random(seed), Random(seed)));
}

float3 SampleCosineHemisphereScaled(inout uint randomSeed, in float scale)
{
    // Generate two uniform random numbers
    float r1 = Random(randomSeed);
    float r2 = Random(randomSeed);

    // Azimuthal angle
    float phi = 2.0f * K_PI * r1;

    // Maximum cone angle
    float cosMax = cos(saturate(scale) * K_PI / 2.0f);

    // Cosine of polar angle within cone
    float cosTheta = lerp(cosMax, 1.0f, sqrt(1.0f - r2)); // cosine-weighted
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

    // Convert to Cartesian coordinates
    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
}

float3 TangentToWorld(float3 normal, float3 tangentSample)
{
    float3 tangent;
    float3 bitangent;
    CreateOrthonormalBasis(normal, tangent, bitangent);

    return tangent * tangentSample.x +
           bitangent * tangentSample.y +
           normal * tangentSample.z;
}

// https://blog.selfshadow.com/publications/blending-in-detail/
// geometric normal s, a base normal t and a secondary (or detail) normal u
float3 ReorientNormal(float3 u, float3 t, float3 s)
{
    // Build the shortest-arc quaternion
    float4 q = float4(cross(s, t), dot(s, t) + 1) / sqrt(2 * (dot(s, t) + 1));

    // Rotate the normal
    return u * (q.w * q.w - dot(q.xyz, q.xyz)) + 2 * q.xyz * dot(q.xyz, u) + 2 * q.w * cross(q.xyz, u);
}

// for when s = (0,0,1)
float3 ReorientNormal(float3 n1, float3 n2)
{
    n1 += float3(0, 0, 1);
    n2 *= float3(-1, -1, 1);

    return n1 * dot(n1, n2) / n1.z - n2;
}

#endif // COMMON_RT_HLSLI
