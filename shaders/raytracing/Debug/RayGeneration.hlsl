#if !(defined(SHARC) && SHARC_UPDATE) && DEBUG_TRACE_HEATMAP
#   define NV_SHADER_EXTN_SLOT u127
#   define NV_SHADER_EXTN_REGISTER_SPACE space0
#   include "include/nvapi/nvHLSLExtns.h"

#   include "include/nvapi/Profiling.hlsli"
#endif

#include "raytracing/Debug/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"

#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

#if defined(GROUP_TILING)
#   define DXC_STATIC_DISPATCH_GRID_DIM 1
#   include "include/ThreadGroupTilingX.hlsli"
#endif

#ifndef THREAD_GROUP_SIZE
#define THREAD_GROUP_SIZE (32)
#endif

// --------------------------------------------------------
// Copied from shaders\raytracing\include\Rays.hlsli
// --------------------------------------------------------

#ifndef RAY_FLAGS
#   define RAY_FLAGS (RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES)
#endif

#ifndef INSTANCE_MASK
#   define INSTANCE_MASK (0xFF)
#endif

Payload TraceRayOpaque(RaytracingAccelerationStructure scene, RayDesc ray, inout uint randomSeed)
{
    Payload payload;
    payload.Init(randomSeed);

#if USE_RAY_QUERY
    RayQuery<RAY_FLAGS | RAY_FLAG_FORCE_OPAQUE> rayQuery;
    rayQuery.TraceRayInline(scene, RAY_FLAG_NONE, INSTANCE_MASK, ray);

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            rayQuery.CommitNonOpaqueTriangleHit();
        }
    }
    
    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        payload.SetCommittedHit(
            rayQuery.CommittedRayT(),
            rayQuery.CommittedPrimitiveIndex(),
            rayQuery.CommittedTriangleBarycentrics(),
            rayQuery.CommittedInstanceIndex(),
            rayQuery.CommittedGeometryIndex());
    }
    
#else // !USE_RAY_QUERY    
    TraceRay(Scene, RAY_FLAGS | RAY_FLAG_FORCE_OPAQUE, INSTANCE_MASK, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
#endif
    
    randomSeed = payload.randomSeed;
    
    return payload;
}

// --------------------------------------------------------

#if USE_RAY_QUERY
[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
#   if defined(GROUP_TILING)
void Main(uint2 GTid : SV_GroupThreadID, uint2 Gid : SV_GroupID)
#   else
void Main(uint2 idx : SV_DispatchThreadID)
#   endif
#else
[shader("raygeneration")]
void Main()
#endif
{
#if USE_RAY_QUERY
    uint2 size = Camera.RenderSize;  
#   if defined(GROUP_TILING)    
    uint2 idx = ThreadGroupTilingX((uint2)ceil(size / THREAD_GROUP_SIZE), THREAD_GROUP_SIZE.xx, 32, GTid.xy, Gid.xy);
#   endif
    if (any(idx >= size))
        return;
#else    
    uint2 idx = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
#endif

    RayDesc sourceRay = SetupPrimaryRay(idx, size, Camera);
    
    const float3 sourceDirection = sourceRay.Direction;
    
    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);

    Payload sourcePayload = TraceRayOpaque(Scene, sourceRay, randomSeed);

    if (!sourcePayload.Hit())
    {
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
          
    float3 sourcePosition = Camera.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
    bool2 pattern = frac(sourcePosition.xy * GAME_UNIT_TO_M) > 0.5;
    const float3 color = (pattern.x ^ pattern.y ? 0.6 : 0.4).rrr;
    
    float3 uvw = GetBary(sourcePayload.Barycentrics());
    
    Instance sourceInstance;
    Mesh sourceMesh = GetMesh(sourcePayload, sourceInstance);
    
    Vertex v0;
    Vertex v1;
    Vertex v2;
    GetVertices(sourceMesh, sourcePayload.primitiveIndex, v0, v1, v2);
    
    //float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));

    float3x3 objectToWorld3x3 = mul((float3x3) sourceInstance.Transform, (float3x3) sourceMesh.Transform);
    
    float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
    float3 tangentWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Tangent, v1.Tangent, v2.Tangent, uvw)));
    //float3 bitangentWS = cross(tangentWS, normalWS) * handedness;
    
    Output[idx] = float4(normalWS * 0.5f + 0.5f, 1.0f);
}
