#include "raytracing/Pathtracing/IndirectLighting/Registers.hlsli"

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
    
    Surface sourceSurface = SurfaceMaker::make(positionWS, faceNormal, normalWS, tangentWS, bitangentWS, albedo.xyz, normalRoughness.w, emissiveMetallic.w, emissiveMetallic.xyz, 0);
    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -positionCS / hitDistance);

    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * hitDistance, Raytracing.PixelConeSpreadAngle);
    
    AdjustShadingNormal(sourceSurface, sourceBRDFContext, false, false);

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, sourceSurface.Normal, sourceBRDFContext.ViewDirection, true);
    
    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    bool isSpecular = false;
    
#if MAX_SAMPLES > 1    
    [loop]
    for (uint i = 0; i < MAX_SAMPLES; i++) 
    {
#else
    {
        const uint i = 0;
#endif

        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);
        
        Surface surface = sourceSurface;
        RayCone rayCone = sourceRayCone;
        
        BRDFContext brdfContext = sourceBRDFContext;
        StandardBSDF bsdf = sourceBSDF;
        
#if MAX_BOUNCES > 1  
        [loop]
        for (uint j = 0; j < MAX_BOUNCES; j++)
        {
#else
        {
            const uint j = 0;
#endif 
            
            BSDFSample bsdfSample;
            
            float3 faceNormalOriented = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;
            
            bool isValid = bsdf.SampleBSDF(brdfContext, materialFeature, surface, bsdfSample, randomSeed);

            [branch]
            if (!isValid)
                break;
            
            float3 direction = bsdfSample.wo;
            
            bool isDelta = bsdfSample.isLobe(LobeType::Delta);
            isSpecular = bsdfSample.isLobe(LobeType::Specular) || isDelta;

            bool hasTransmission = bsdfSample.isLobe(LobeType::Transmission);

            throughput *= bsdfSample.isLobe(LobeType::Transmission) ? 1.f : surface.AO;
            
            RayDesc ray;
            
#if USE_SIA_INTERPOLATION
            ray.Origin = OffsetRaySIA(surface.Position, faceNormalOriented, surface.SIAOffset, hasTransmission);
#else
            ray.Origin = OffsetRay(surface.Position, faceNormalOriented, surface.PositionError, hasTransmission);
#endif
            ray.Direction = direction;
            ray.TMin = 0.0f; // Offset already handles precision, no additional offset needed
            ray.TMax = RAY_TMAX;

            Payload payload = TraceRayStandard(Scene, ray, randomSeed);  
            
            if (!payload.Hit())
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;
                sampleRadiance += skyIrradiance * throughput;          
            }
            else
            {
                float3 localPosition = ray.Origin + direction * payload.hitDistance;

                Instance instance;              
                LightingMaterialData materialData;
                surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, materialData, false);
                
                brdfContext = BRDFContext::make(surface, -direction);
                const bool isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
                if (!isEnter)
                {
                    surface.FlipNormal();
                    brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
                }

                AdjustShadingNormal(surface, brdfContext, true, false); // Adjusts the normal of the supplied shading frame to reduce black pixels due to back-facing view direction.
                StandardBSDF bsdf = StandardBSDF::make(surface, surface.Normal, brdfContext.ViewDirection, isEnter);

                const float3 directRadiance = EvaluateDirectRadiance(materialType, materialFeature, surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
                
                sampleRadiance += directRadiance * throughput;
                sampleRadiance += surface.Emissive * throughput;
            }
        }
        
        radiance += sampleRadiance;
    }
    
#if MAX_SAMPLES > 1    
    radiance /= MAX_SAMPLES;
#endif
    
    const half3 indirect = half3(radiance);
    Output[idx].xyz += indirect;
}
