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
    
    half3 radiance = half3(0.0h, 0.0h, 0.0h);
    
#if MAX_SAMPLES > 1    
    [loop]
    for (uint16_t i = 0; i < MAX_SAMPLES; i++) 
    {
#else
    {
        const uint16_t i = 0;
#endif
        half3 sampleRadiance = half3(0.0h, 0.0h, 0.0h);

        BSDFSample firstSample;
        [branch]
        if (sourceBSDF.SampleBSDF(sourceBRDFContext, materialFeature, sourceSurface, firstSample, randomSeed))
        {
            half3 throughput = (half3)firstSample.weight * (firstSample.isLobe(LobeType::Transmission) ? 1.0h : (half)sourceSurface.AO);
            float3 firstFaceNormal = dot(sourceBRDFContext.ViewDirection, sourceSurface.FaceNormal) >= 0.0f ? sourceSurface.FaceNormal : -sourceSurface.FaceNormal;
            bool firstHasTransmission = firstSample.isLobe(LobeType::Transmission);

            float3 rayOrigin;
#if USE_SIA_INTERPOLATION
            rayOrigin = OffsetRaySIA(sourceSurface.Position, firstFaceNormal, sourceSurface.SIAOffset, firstHasTransmission);
#else
            rayOrigin = OffsetRay(sourceSurface.Position, firstFaceNormal, sourceSurface.PositionError, firstHasTransmission);
#endif
            half3 rayDirection = (half3)firstSample.wo;

            RayCone rayCone = sourceRayCone;
            if (!firstSample.isLobe(LobeType::Delta))
                rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(firstSample.pdf), (float)(2.0 * K_PI)));

            [loop]
            for (uint16_t j = 0; j < MAX_BOUNCES; j++)
            {               
                RayDesc ray;
                ray.Origin = rayOrigin;
                ray.Direction = (float3)rayDirection;
                ray.TMin = 0.0f;
                ray.TMax = RAY_TMAX;

                Payload payload = TraceRayStandard(Scene, ray, randomSeed);  
                rayCone = rayCone.propagateDistance(payload.hitDistance);
                
                if (!payload.Hit())
                {
                    half3 skyIrradiance = (half3)(SampleSky(SkyHemisphere, (float3)rayDirection) * Raytracing.Sky);
                    sampleRadiance += skyIrradiance * throughput;
                    break;
                }

                float3 localPosition = rayOrigin + (float3)rayDirection * payload.hitDistance;

                Instance instance;              
                LightingMaterialData materialData;
                Surface surface = SurfaceMaker::make(localPosition, payload, (float3)rayDirection, rayCone, instance, materialData, false);
                
                BRDFContext brdfContext = BRDFContext::make(surface, -(float3)rayDirection);
                const bool isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
                if (!isEnter)
                {
                    surface.FlipNormal();
                    brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
                }

                AdjustShadingNormal(surface, brdfContext, true, false);
                StandardBSDF bsdf = StandardBSDF::make(surface, surface.Normal, brdfContext.ViewDirection, isEnter);

                const half3 directRadiance = (half3)EvaluateDirectRadiance(materialData.Type, materialData.Feature, surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
                sampleRadiance += (directRadiance + (half3)surface.Emissive) * throughput;

                if (j == MAX_BOUNCES - 1)
                    break;

                BSDFSample nextSample;
                if (!bsdf.SampleBSDF(brdfContext, materialData.Feature, surface, nextSample, randomSeed))
                    break;

                throughput *= (half3)nextSample.weight * (nextSample.isLobe(LobeType::Transmission) ? 1.0h : (half)surface.AO);
                
#   if RUSSIAN_ROULETTE == 1
                {
                    const half rrVal = 1.0h - min(1.0h, (half)Color::RGBToLuminance(throughput));              
                    const half rrProb = min(rrVal, 0.95h);

                    if ((half)Random(randomSeed) < rrProb)
                        break;

                    throughput /= (1.0h - rrProb); 
                }
#   elif RUSSIAN_ROULETTE == 2
                {
                    const half rrVal = (half)sqrt(Color::RGBToLuminance(throughput));            
                    half rrProb = saturate(0.85h - rrVal);
                    rrProb *= rrProb;

                    rrProb = saturate(rrProb + max(0.0h, ((half)j / (half)MAX_BOUNCES - 0.4h)));

                    if ((half)Random(randomSeed) < rrProb)
                        break;

                    throughput /= (1.0h - rrProb);
                }
#   endif

                const float3 hitFaceNormal = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;
                const bool hitHasTransmission = nextSample.isLobe(LobeType::Transmission);

#if USE_SIA_INTERPOLATION
                rayOrigin = OffsetRaySIA(surface.Position, hitFaceNormal, surface.SIAOffset, hitHasTransmission);
#else
                rayOrigin = OffsetRay(surface.Position, hitFaceNormal, surface.PositionError, hitHasTransmission);
#endif
                rayDirection = (half3)nextSample.wo;

                if (!nextSample.isLobe(LobeType::Delta))
                    rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(nextSample.pdf), (float)(2.0 * K_PI)));
            }
        }
        
        radiance += sampleRadiance;
    }
    
#if MAX_SAMPLES > 1    
    radiance /= (half)MAX_SAMPLES;
#endif
    
    Output[idx] += half4(Output[idx].xyz + radiance, 0.0f);
}
