#include "raytracing/include/Common.hlsli"
#include "raytracing/include/Registers.hlsli"
#include "raytracing/include/Payload.hlsli"

[shader("raygeneration")]
void Main()
{
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
    
    RayDesc ray = SetupPrimaryRay(idx, size);
    
    Payload payload;
    payload.hitDistance = -1.0f;
    payload.primitiveIndex = 0;
    payload.PackBarycentrics(float2(0.0f, 0.0f));
    payload.PackInstanceGeometryIndex(0, 0);
    payload.randomSeed = 0;    
    
    TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);
    
    float hit = payload.Hit() ? 1.0f : 0.0f;
    
    Output[idx] = float4(hit, hit, hit, 1.0f);
}