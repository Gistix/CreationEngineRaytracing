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
    
#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_NONE> rayQuery;
    rayQuery.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, ray);

    /*while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (considerTransparentMaterial(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidateTriangleBarycentrics()))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }*/

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.hitDistance = rayQuery.CommittedRayT();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.PackBarycentrics(rayQuery.CommittedTriangleBarycentrics());
        payload.PackInstanceGeometryIndex(rayQuery.CommittedInstanceIndex(), rayQuery.CommittedGeometryIndex());    
      
    }

#else // !USE_RAY_QUERY    
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
#endif
    
    float hit = payload.Hit() ? 1.0f : 0.0f;
    
    float2 uv = (idx + 0.5f) / size;
    
    Output[idx] = float4(hit, uv, 1.0f);
}