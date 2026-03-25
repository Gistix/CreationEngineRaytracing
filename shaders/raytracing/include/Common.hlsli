#ifndef COMMON_RT_HLSLI
#define COMMON_RT_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/Vertex.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

#include "include/Common.hlsli"

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
    return coord.x + coord.y * size.x + frameCount * 719393;
}

uint PCGHash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random(inout uint seed)
{
    seed = PCGHash(seed);
    return float(seed) / 4294967296.0; // Divide by 2^32
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

float3 SampleCosineHemisphere(inout uint seed)
{
    float u1 = Random(seed);
    float u2 = Random(seed);

    float r = sqrt(u1);
    float theta = 2.0 * K_PI * u2;

    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(1.0 - u1);

    return float3(x, y, z);
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

void CreateOrthonormalBasis(in float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);

    tangent = normalize(cross(up, normal));
    bitangent = cross(normal, tangent);
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

#endif // COMMON_RT_HLSLI