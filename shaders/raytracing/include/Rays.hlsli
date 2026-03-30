#ifndef RAYS_HLSL
#define RAYS_HLSL

#include "raytracing/include/RayOffset.hlsli"

#include "include/Surface.hlsli"
#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "raytracing/include/ShadowPayload.hlsli"

#include "raytracing/include/Transparency.hlsli"

#define RAY_FLAGS (RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES)

Payload TraceRayOpaque(RaytracingAccelerationStructure scene, RayDesc ray, inout uint randomSeed)
{
    Payload payload;
    payload.hitDistance = -1.0f;
    payload.primitiveIndex = 0;
    payload.PackBarycentrics(float2(0.0f, 0.0f));
    payload.PackInstanceGeometryIndex(0, 0);
    payload.randomSeed = randomSeed;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAGS | RAY_FLAG_FORCE_OPAQUE> rayQuery;
    rayQuery.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);

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
        payload.hitDistance = rayQuery.CommittedRayT();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.PackBarycentrics(rayQuery.CommittedTriangleBarycentrics());
        payload.PackInstanceGeometryIndex(rayQuery.CommittedInstanceIndex(), rayQuery.CommittedGeometryIndex());
    }
#else // !USE_RAY_QUERY    
    TraceRay(Scene, RAY_FLAGS | RAY_FLAG_FORCE_OPAQUE, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
 #endif
    
    randomSeed = payload.randomSeed;
    
    return payload;
}

Payload TraceRayStandard(RaytracingAccelerationStructure scene, RayDesc ray, inout uint randomSeed)
{
    Payload payload;
    payload.hitDistance = -1.0f;
    payload.primitiveIndex = 0;
    payload.PackBarycentrics(float2(0.0f, 0.0f));
    payload.PackInstanceGeometryIndex(0, 0);
    payload.randomSeed = randomSeed;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAGS> rayQuery;
    rayQuery.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);

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
        payload.hitDistance = rayQuery.CommittedRayT();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.PackBarycentrics(rayQuery.CommittedTriangleBarycentrics());
        payload.PackInstanceGeometryIndex(rayQuery.CommittedInstanceIndex(), rayQuery.CommittedGeometryIndex());
    }
#else // !USE_RAY_QUERY    
    TraceRay(Scene, RAY_FLAGS, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
 #endif
    
    randomSeed = payload.randomSeed;
    
    return payload;
}

float3 TraceRayShadow(RaytracingAccelerationStructure scene, Surface surface, float3 direction, inout uint randomSeed)
{
    RayDesc ray;
    bool hasTransmission = any(surface.TransmissionColor > 0.0f) && dot(surface.FaceNormal, direction) < 0.0f;
    ray.Origin = OffsetRay(surface.Position, surface.FaceNormal, surface.PositionError, hasTransmission);
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = SHADOW_RAY_TMAX;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
    shadowPayload.randomSeed = randomSeed;
    shadowPayload.transmission = float3(1.0f, 1.0f, 1.0f);

#if USE_RAY_QUERY
    RayQuery<RAY_FLAGS> rayQuery;
    rayQuery.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (ConsiderTransparentMaterialShadow(
                rayQuery.CandidateInstanceIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),   
                rayQuery.CandidateTriangleBarycentrics(),
                randomSeed,
                direction,
                rayQuery.CandidateTriangleRayT(),
                shadowPayload.transmission))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        shadowPayload.missed = 1.0f;
    }
#else // !USE_RAY_QUERY    
    TraceRay(scene, RAY_FLAGS | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
 #endif
    
    randomSeed = shadowPayload.randomSeed;
    return shadowPayload.transmission * shadowPayload.missed;
}

float3 TraceRayShadowFinite(RaytracingAccelerationStructure scene, Surface surface, float3 direction, float tmax, inout uint randomSeed)
{
    RayDesc ray;
    bool hasTransmission = any(surface.TransmissionColor > 0.0f) && dot(surface.FaceNormal, direction) < 0.0f;
    ray.Origin = OffsetRay(surface.Position, surface.FaceNormal, surface.PositionError, hasTransmission);
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = tmax;

    ShadowPayload shadowPayload;
    shadowPayload.missed = 0.0f;
    shadowPayload.randomSeed = randomSeed;
    shadowPayload.transmission = float3(1.0f, 1.0f, 1.0f);

#if USE_RAY_QUERY
    RayQuery<RAY_FLAGS> rayQuery;
    rayQuery.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            if (ConsiderTransparentMaterialShadow(
                rayQuery.CandidateInstanceIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidatePrimitiveIndex(),   
                rayQuery.CandidateTriangleBarycentrics(),
                randomSeed,
                direction,
                rayQuery.CandidateTriangleRayT(),
                shadowPayload.transmission))
            {
                rayQuery.CommitNonOpaqueTriangleHit();
            }
        }
    }

    if (rayQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    {
        shadowPayload.missed = 1.0f;
    }
#else // !USE_RAY_QUERY    
    TraceRay(scene, RAY_FLAGS | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xFF, SHADOW_RAY_HITGROUP_IDX, 0, SHADOW_RAY_MISS_IDX, ray, shadowPayload);
 #endif
    
    randomSeed = shadowPayload.randomSeed;
    
    return shadowPayload.transmission * shadowPayload.missed;
}

Payload SampleSubsurface(RaytracingAccelerationStructure scene, const float3 samplePosition, const float3 surfaceNormal, const float tmax, inout uint randomSeed)
{
    RayDesc ray;
    ray.Origin = samplePosition;
    ray.Direction = -surfaceNormal; // Shooting ray towards the surface
    ray.TMin = 0.0f;
    ray.TMax = tmax;

    Payload payload;
    payload.hitDistance = -1.0f;
    payload.primitiveIndex = 0;
    payload.PackBarycentrics(float2(0.0f, 0.0f));
    payload.PackInstanceGeometryIndex(0, 0);
    payload.randomSeed = randomSeed;

#if USE_RAY_QUERY
    RayQuery<RAY_FLAGS> rayQuery;
    rayQuery.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);

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
        payload.hitDistance = rayQuery.CommittedRayT();
        payload.primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        payload.PackBarycentrics(rayQuery.CommittedTriangleBarycentrics());
        payload.PackInstanceGeometryIndex(rayQuery.CommittedInstanceIndex(), rayQuery.CommittedGeometryIndex());    
      
    }
    
#else // !USE_RAY_QUERY    
    TraceRay(scene, RAY_FLAGS, 0xFF, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
 #endif   
    
    randomSeed = payload.randomSeed;

    return payload;
}

#endif // RAYS_HLSL