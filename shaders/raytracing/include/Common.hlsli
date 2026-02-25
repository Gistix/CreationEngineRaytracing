#ifndef COMMON_RT_HLSLI
#define COMMON_RT_HLSLI

#include "interop/CameraData.hlsli"
#include "interop/Vertex.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

RayDesc SetupPrimaryRay(uint2 idx, uint2 size, CameraData camera)
{
    const float2 uv = float2(idx + 0.5f) / size;
    
    float2 screenPos = uv * 2.0f - 1.0f;
    screenPos.y = -screenPos.y;

    const float4 clip = float4(screenPos, 1.0f, 1.0f);
    float4 view = mul(camera.ProjInverse, clip);
    view /= view.w;

    RayDesc ray;
    ray.Origin = camera.Position.xyz;
    ray.Direction = normalize(mul((float3x3)camera.ViewInverse, view.xyz));
    ray.TMin = 0.1f;
    ray.TMax = 1e30;
    
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

#endif // COMMON_RT_HLSLI