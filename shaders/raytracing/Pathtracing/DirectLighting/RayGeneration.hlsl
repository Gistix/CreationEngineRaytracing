#include "raytracing/Pathtracing/DirectLighting/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "raytracing/include/Payload.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "include/Lighting.hlsli"
#include "raytracing/include/Materials/BSDF.hlsli"

#include "raytracing/include/Rays.hlsli"

#if defined(GROUP_TILING)
#   define DXC_STATIC_DISPATCH_GRID_DIM 1
#   include "include/ThreadGroupTilingX.hlsli"
#endif

#if USE_RAY_QUERY
WAVE_SIZE(32)
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
    
    const half4 albedo = Albedo[idx];
    
    [branch]
    if (albedo.a < 0.5f)
    {
        Output[idx] = half4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    
    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);
    
    const float depth = Depth[idx];
    const half4 emissiveMetallic = EmissiveMetallic[idx];
    const half4 normalRoughness = NormalRoughness[idx];
    const uint material = Material[idx];
    
    const uint16_t materialType = uint16_t(material & 0xFFu);
    const uint16_t materialFeature = uint16_t((material >> 8) & 0xFFu);
    const uint16_t unused1 = uint16_t((material >> 16) & 0xFFu);
    const uint16_t unused2 = uint16_t((material >> 24) & 0xFFu);
    
    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    
    const float2 uv = float2(idx + 0.5f) / size;
    
    const float3 positionVS = ScreenToViewPosition(uv, depthVS, Camera.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Camera.ViewInverse);
    const float3 positionWS = positionCS + Camera.Position.xyz;
    
    const float hitDistance = length(positionCS);

    const float3 normalWS = normalRoughness.xyz;

    float3 tangentWS, bitangentWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangentWS);
    
    const float3 faceNormal = normalWS;
    
    Surface surface = SurfaceMaker::make(positionWS, faceNormal, normalWS, tangentWS, bitangentWS, albedo.xyz, normalRoughness.w, emissiveMetallic.w, emissiveMetallic.xyz, 0);
    BRDFContext brdfContext = BRDFContext::make(surface, -positionCS / hitDistance);

    AdjustShadingNormal(surface, brdfContext, false, false);

    StandardBSDF bsdf = StandardBSDF::make(surface, surface.Normal, brdfContext.ViewDirection, true);

    float3 radiance = EvalDirectionalLight(materialType, materialFeature, surface, brdfContext, bsdf, randomSeed);
    radiance += EvalGlobalPointLight(materialType, materialFeature, surface, brdfContext, bsdf, randomSeed);

    const half3 direct = half3(albedo.xyz * radiance);
    Output[idx] = half4(direct, 1.0f);
}
