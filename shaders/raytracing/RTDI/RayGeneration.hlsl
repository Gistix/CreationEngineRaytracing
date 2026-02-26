#include "raytracing/Pathtracing/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "include/Lighting.hlsli"

#include "raytracing/include/Transparency.hlsli"

#if USE_RAY_QUERY
[numthreads(16, 16, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void Main()
#endif
{
#if USE_RAY_QUERY
    uint2 size = Camera.RenderSize;
    
    if (any(idx >= size))
        return;
#else    
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
#endif
    
    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);
    
    RayDesc sourceRay = SetupPrimaryRay(idx, size, Camera);
    
    const float3 sourceDirection = sourceRay.Direction;
    
    Payload sourcePayload;
    sourcePayload.hitDistance = -1.0f;
    sourcePayload.primitiveIndex = 0;
    sourcePayload.PackBarycentrics(float2(0.0f, 0.0f));
    sourcePayload.PackInstanceGeometryIndex(0, 0);
    sourcePayload.randomSeed = randomSeed;   
    
#if USE_RAY_QUERY
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
    rayQuery.TraceRayInline(Scene, RAY_FLAG_NONE, 0xFF, sourceRay);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (ConsiderTransparentMaterial(
                rayQuery.CandidateInstanceIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),   
                rayQuery.CandidateTriangleBarycentrics(),
                randomSeed))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        sourcePayload.hitDistance = rayQuery.CommittedRayT();
        sourcePayload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        sourcePayload.PackBarycentrics(rayQuery.CommittedTriangleBarycentrics());
        sourcePayload.PackInstanceGeometryIndex(rayQuery.CommittedInstanceIndex(), rayQuery.CommittedGeometryIndex());
    }

#else // !USE_RAY_QUERY    
    TraceRay(Scene, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, 0, 0, 0, sourceRay, sourcePayload);
#endif
    
    randomSeed = sourcePayload.randomSeed;
    
    if (!sourcePayload.Hit())
    {
#if defined(SHARC) && defined(SHARC_UPDATE)
        return;
#endif
        
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
          
    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * sourcePayload.hitDistance, Raytracing.PixelConeSpreadAngle);   
    
    float3 sourcePosition = Camera.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
    Instance sourceInstance;
    Material sourceMaterial;
    
    Surface sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial);

    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -sourceDirection);
    
    if (dot(sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection) < 0.0f) 
        sourceSurface.FlipNormal();

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, true);    
    
    AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);    
    
    bool isSssPath = false;
    
    float3 direct = sourceSurface.Emissive;
#ifdef SUBSURFACE_SCATTERING
    if (sourceSurface.SubsurfaceData.HasSubsurface != 0) {
        direct += EvaluateSubsurfaceNEE(sourceSurface, sourceBRDFContext, sourceMaterial, sourceInstance, sourcePayload, sourceRayCone, randomSeed);
        isSssPath = true;
    }
    else
#endif
        direct += EvaluateDirectRadiance(sourceMaterial, sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed);    
    
    Output[idx] = float4(direct, 1.0f);
}