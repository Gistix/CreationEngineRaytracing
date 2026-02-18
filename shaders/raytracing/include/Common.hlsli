#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "raytracing/include/Registers.hlsli"

RayDesc SetupPrimaryRay(uint2 idx, uint2 size)
{
    const float2 uv = float2(idx + 0.5f) / size;
    
    float2 screenPos = uv * 2.0f - 1.0f;
    screenPos.y = -screenPos.y;

    const float4 clip = float4(screenPos, 1.0f, 1.0f);
    float4 view = mul(Frame.ProjInverse, clip);
    view /= view.w;

    RayDesc ray;
    ray.Origin = Frame.Position.xyz;
    ray.Direction = normalize(mul((float3x3)Frame.ViewInverse, view.xyz));
    ray.TMin = 0.1f;
    ray.TMax = 1e30;
    
    return ray;
}

#endif // COMMON_HLSLI