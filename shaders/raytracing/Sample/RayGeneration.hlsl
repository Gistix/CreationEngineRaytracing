#include "raytracing/include/Common.hlsli"
#include "raytracing/include/Registers.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

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

    while (rayQuery.Proceed())
    {
        if (rayQuery.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            /*if (considerTransparentMaterial(
                rayQuery.CandidateInstanceID(),
                rayQuery.CandidatePrimitiveIndex(),
                rayQuery.CandidateGeometryIndex(),
                rayQuery.CandidateTriangleBarycentrics()))
            {*/
                rayQuery.CommitNonOpaqueTriangleHit();
            //}
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
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
#endif
    
    float3 color = float3(0.0f, 0.0f, 0.0f);
    
    if (payload.Hit())
    {
        float3 uvw = GetBary(payload.Barycentrics());

        Instance instance;
        Mesh mesh = GetMesh(payload, instance);
        
        Material material = mesh.Material;
        
        Vertex v0, v1, v2;
        GetVertices(mesh.GeometryIdx, payload.primitiveIndex, v0, v1, v2);       
        
        float2 texCoord0 = material.TexCoord(Interpolate(v0.Texcoord0, v1.Texcoord0, v2.Texcoord0, uvw));
        
        float3x3 objectToWorld3x3 = mul((float3x3) instance.Transform, (float3x3) mesh.Transform);
        
        float3 normalWS = normalize(mul(objectToWorld3x3, Interpolate(v0.Normal, v1.Normal, v2.Normal, uvw)));
        
        Texture2D baseTexture = Textures[NonUniformResourceIndex(material.BaseTexture())];
        float3 albedo = baseTexture.SampleLevel(DefaultSmapler, texCoord0, 0).rgb;
        
        color = albedo;
    }
    
    Output[idx] = float4(color, 1.0f);
}