#include "raytracing/GlobalIllumination/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "include/Lighting.hlsli"

#ifdef SUBSURFACE_SCATTERING
#include "raytracing/include/SubsurfaceLighting.hlsli"
#endif

#include "raytracing/include/Transparency.hlsli"

#include "Raytracing/Include/SHARC/Sharc.hlsli"
#include "Raytracing/Include/SHARC/SHaRCHelper.hlsli"

#if defined(GROUP_TILING)
#   define DXC_STATIC_DISPATCH_GRID_DIM 1
#   include "include/ThreadGroupTilingX.hlsli"
#endif

#ifndef THREAD_GROUP_SIZE
#define THREAD_GROUP_SIZE (32)
#endif

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

#if defined(SHARC)
    SharcParameters sharcParameters = GetSharcParameters();

#    if SHARC_UPDATE
        uint startIndex = Hash(idx) % 25;

        uint2 blockOrigin = idx * 5;

        uint pixelIndex = (startIndex + Camera.FrameIndex) % 25;

        idx = blockOrigin + uint2(pixelIndex % 5, pixelIndex / 5);

        if (any(idx >= Camera.RenderSize))
            return;

        size = Camera.RenderSize;
#   endif

#endif    
    
    const float2 uv = float2(idx + 0.5f) / size;

    // If the game has dynamic resolution enabled the textures will not cover the entire extent
    const float2 dynamicUV = float2(idx + 0.5f) / Camera.ScreenSize;   
    
    const float2 dynamicUVUnjittered = dynamicUV - (Camera.Jitter / Camera.ScreenSize);

    const float depth = Depth.SampleLevel(DefaultSampler, dynamicUVUnjittered, 0) * DEPTH_SCALE;
    
    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);

    [branch]
    if (depthVS < FP_VIEW_Z || depth >= SKY_Z)
    {
#if !(defined(SHARC) && SHARC_UPDATE)
#   if defined(RAW_RADIANCE)
        DiffuseOutput[idx] = float3(0.0f, 0.0f, 0.0f);
        SpecularOutput[idx] = float3(0.0f, 0.0f, 0.0f);   
        
#   else
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);  
#   endif        

#   if defined(DLSS_RR)
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
        SpecularHitDistance[idx] = RAY_TMAX;
#   endif
#endif
        return;
    }    
    
    const snorm float4 normalRoughness = NormalRoughness.SampleLevel(DefaultSampler, dynamicUV, 0);
    
    const unorm float linearRoughness = normalRoughness.w;
    
    const unorm float4 normalMetalnessAO = GNMAO.SampleLevel(DefaultSampler, dynamicUV, 0);

    const half3 geometryNormalVS = DecodeNormal((half2)normalMetalnessAO.xy);
    const float3 geometryNormalWS = normalize(ViewToWorldVector(geometryNormalVS, Camera.ViewInverse));    
    
    const float metalness = normalMetalnessAO.z;
    const float ao = 1.0f;
    
    const float3 positionVS = ScreenToViewPosition(uv, depthVS, Camera.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Camera.ViewInverse);
    const float3 positionWS = positionCS + Camera.Position.xyz;    
    
    const float hitDistance = length(positionCS);
    
    const snorm float3 normalWS = normalRoughness.xyz;    
    
    float3 tangentWS, bitangentWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangentWS);    

    float3 albedo = LLGammaToTrueLinear(Albedo.SampleLevel(DefaultSampler, dynamicUV, 0).rgb);

    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * hitDistance, Raytracing.PixelConeSpreadAngle);
    
    Surface sourceSurface = SurfaceMaker::make(positionWS, geometryNormalWS, normalWS, tangentWS, bitangentWS, albedo, linearRoughness, metalness, 0, ao);
    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -positionCS / hitDistance);

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, true);     
    
    AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);    

    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);   
    
    bool isSssPath = false;

    float3 direction;
    MonteCarlo::BRDFWeight brdfWeight;

    float3 radiance = 0;
    bool isSpecular = false;
    float specHitDist = 0;

    RayDesc ray;
    Payload payload;

    Instance instance;
    Material material;

    Surface surface;
    BRDFContext brdfContext;

    StandardBSDF bsdf;
    
    RayCone rayCone;    
    
#if defined(SHARC)
    SharcState sharcState;
    SharcHitData sharcHitData;
#endif    
    
    [loop]
    for (uint i = 0; i < MAX_SAMPLES; i++)
    {
#if defined(SHARC) && SHARC_UPDATE
        SharcInit(sharcState);
#endif
        
        surface = sourceSurface;
        brdfContext = sourceBRDFContext;
        bsdf = sourceBSDF;
        rayCone = sourceRayCone; 

        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);
        float materialRoughnessPrev = 0.0f;
        bool isEnter = true;

        // Water volume tracking for Beer-Lambert absorption
        bool insideWaterVolume = false;
        float3 waterVolumeAbsorption = float3(0.0f, 0.0f, 0.0f);
        
#if defined(RAW_RADIANCE)
        float3 throughputDelta = float3(1.0f, 1.0f, 1.0f);
#endif        
        
        [loop]
        for (uint j = 0; j < MAX_BOUNCES; j++)
        {
            BSDFSample bsdfSample;
            
            float3 faceNormalOriented = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;            
            
#if LIGHTING_MODE == LIGHTING_MODE_DIFFUSE
            direction = surface.Mul(SampleCosineHemisphere(randomSeed));

            float NdotD = saturate(dot(surface.Normal, direction));

            throughput *= surface.AO;
            throughput *= surface.Albedo;
            
            const bool hasTransmission = false;
#else            
            bool isValid = bsdf.SampleBSDF(brdfContext, material, surface, bsdfSample, randomSeed);
            bool isDelta = bsdfSample.isLobe(LobeType::Delta);
            isSpecular = bsdfSample.isLobe(LobeType::Specular) || isDelta;
            bool hasTransmission = bsdfSample.isLobe(LobeType::Transmission);

            if (isValid)
                direction = bsdfSample.wo;
            else
                break;

            throughput *= bsdfSample.isLobe(LobeType::Transmission) ? 1.f : surface.AO;

            // Track water volume entry/exit on transmission
            if (hasTransmission && any(surface.VolumeAbsorption > 0.0f))
            {
                // isEnter (front face) + transmission = entering volume
                insideWaterVolume = isEnter;
                waterVolumeAbsorption = insideWaterVolume ? surface.VolumeAbsorption : float3(0.0f, 0.0f, 0.0f);
            }

            brdfWeight.diffuse = bsdfSample.isLobe(LobeType::DiffuseReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
#   if defined(RAW_RADIANCE)
            brdfWeight.diffuse /= max(surface.DiffuseAlbedo, 1e-4f);
#   endif
            brdfWeight.specular = (bsdfSample.isLobe(LobeType::SpecularReflection) || bsdfSample.isLobe(LobeType::DeltaReflection)) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            brdfWeight.transmission = bsdfSample.isLobe(LobeType::Transmission) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            
#   if defined(RAW_RADIANCE)
            float3 brdfWeightOriginal = brdfWeight.diffuse * surface.DiffuseAlbedo + brdfWeight.specular + brdfWeight.transmission;

#       if defined(SHARC) && SHARC_UPDATE
            throughput *= brdfWeightOriginal;
#       else
            if (j > 0) {
                throughput *= brdfWeightOriginal;
            } else {
                float3 brdfWeightRaw = bsdfSample.weight;

                throughputDelta = brdfWeightOriginal / brdfWeightRaw;

                throughput *= brdfWeightRaw;
            }
#       endif
#   else    // RAW_RADIANCE
            throughput *= bsdfSample.weight;
#   endif   // !RAW_RADIANCE
#endif  
            
#if defined(SHARC) && SHARC_UPDATE
            SharcSetThroughput(sharcState, throughput);
#else
            if (Raytracing.RussianRoulette == 1)
            {
                float3 throughputColor;

#   if defined(RAW_RADIANCE)
                throughputColor = throughput * throughputDelta;
#   else
                throughputColor = throughput;
#   endif
                const float rrVal = sqrt(Color::RGBToLuminance(throughputColor));
                float rrProb = saturate(0.85 - rrVal);
                rrProb *= rrProb;

                rrProb = saturate(rrProb + max(0, ((float)j / (float)MAX_BOUNCES - 0.4f)));

                if (Random(randomSeed) < rrProb)
                    break;

                throughput /= (1.0f - rrProb);
            }
#endif
            
#if defined(SHARC)
            materialRoughnessPrev += bsdfSample.isLobe(LobeType::Diffuse) ? 1.0f : surface.Roughness;
#endif
            
            ray.Origin = OffsetRay(surface.Position, faceNormalOriented, hasTransmission);
            ray.Direction = direction;
            ray.TMin = 0.0f;  // OffsetRay already handles precision, no additional offset needed
            ray.TMax = RAY_TMAX;

            if (!bsdfSample.isLobe(LobeType::Delta))
                rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(bsdfSample.pdf), 2.0 * K_PI));

            payload = TraceRayStandard(Scene, ray, randomSeed);
            
            rayCone = rayCone.propagateDistance(payload.hitDistance);

            // Apply Beer-Lambert volume absorption for water
            if (insideWaterVolume)
            {
                throughput *= exp(-waterVolumeAbsorption * payload.hitDistance);
            }
            
            if (isSpecular)
                specHitDist += payload.hitDistance;

            if (!payload.Hit())
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;

#if defined(SHARC) && SHARC_UPDATE
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#else
                sampleRadiance += skyIrradiance * throughput;
#endif                
                break;
            }
            
            float3 localPosition = ray.Origin + direction * payload.hitDistance;

            surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, material, false);

#if defined(SHARC)
            sharcHitData.positionWorld = surface.Position;
            sharcHitData.normalWorld = surface.GeomNormal;

#   if SHARC_SEPARATE_EMISSIVE
            sharcHitData.emissive = surface.Emissive;
#   endif // SHARC_SEPARATE_EMISSIVE

#   if !SHARC_UPDATE
            uint gridLevel = HashGridGetLevel(surface.Position, sharcParameters.gridParameters);
            float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);
            bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);
            
            const bool oldValidHit = isValidHit;
            
            if (isValidHit) {
                materialRoughnessPrev = min(materialRoughnessPrev, 0.99f);
                float a2 = materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev;
                float footprint = payload.hitDistance * sqrt(0.5f * a2 / max(1.0f - a2, DIV_EPSILON));
                isValidHit &= footprint > voxelSize;
            }

            float3 sharcRadiance;
            if (isValidHit && SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
            {
                sampleRadiance += sharcRadiance * throughput;
                break;
            }
#   endif // !SHARC_UPDATE
#endif // SHARC  
            
            brdfContext = BRDFContext::make(surface, -direction);
            isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
            if (!isEnter) surface.FlipNormal();

            AdjustShadingNormal(surface, brdfContext, true, false);  // Adjusts the normal of the supplied shading frame to reduce black pixels due to back-facing view direction.
            bsdf = StandardBSDF::make(surface, isEnter);

            // Direct lighting with delta lobe support
            float3 directRadiance = 0.0f;
            const uint bounceLobes = bsdf.GetLobes(surface);
            const bool bounceHasNonDeltaLobes = (bounceLobes & (uint)LobeType::NonDelta) != 0;
            const bool bounceHasDeltaLobes = (bounceLobes & (uint)LobeType::Delta) != 0;
            
            if (bounceHasNonDeltaLobes)
            {
#ifdef SUBSURFACE_SCATTERING
                if (surface.SubsurfaceData.HasSubsurface != 0 && !isSssPath) {
                    directRadiance += EvaluateSubsurfaceDiffuseNEE(surface, brdfContext, material, instance, payload, rayCone, randomSeed, false);
                    isSssPath = true;
                    // Specular uses the standard path with diffuse suppressed
                    Surface specSurface = surface;
                    specSurface.DiffuseAlbedo = 0;
                    StandardBSDF specBsdf = StandardBSDF::make(specSurface, isEnter);
                    directRadiance += EvaluateDirectRadiance(material, specSurface, brdfContext, instance, specBsdf, randomSeed, true);
                }
                else
#endif
                { 
                    directRadiance += EvaluateDirectRadiance(material, surface, brdfContext, instance, bsdf, randomSeed, true);
                }
            }
            
            // Delta lobe lighting: check if delta reflection/refraction directions see any analytical lights
            if (bounceHasDeltaLobes)
            {
                directRadiance += EvalDeltaLobeLighting(surface, brdfContext, instance, bsdf, randomSeed, true);
            }
            
            sampleRadiance += directRadiance * throughput;

#if defined(SHARC) && SHARC_UPDATE
            if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, directRadiance, Random(randomSeed)))
                return;

            throughput = float3(1.0f, 1.0f, 1.0f);
#else
            sampleRadiance += surface.Emissive * throughput;
#endif
        }

        radiance += sampleRadiance;

#if defined(SHARC) && SHARC_UPDATE
        return;
#endif
    }

    radiance /= MAX_SAMPLES;        
      
    
#if !(defined(SHARC) && SHARC_UPDATE)
#   if defined(RAW_RADIANCE) || defined(DLSS_RR)
    const float2 envBRDF = BRDF::EnvBRDF(sourceSurface.Roughness, sourceBRDFContext.NdotV);
    const float3 specularAlbedo = float3(sourceSurface.F0 * envBRDF.x + envBRDF.y);
#   endif
    
#   if defined(RAW_RADIANCE)
    DiffuseOutput[idx] = float3(isSpecular ? 0.0f : radiance);
    SpecularOutput[idx] = float3(isSpecular ? radiance * specularAlbedo : 0.0f);
#   else
    Output[idx] = float4(radiance, 1.0f);
#   endif
    
#   if defined(DLSS_RR) 
    SpecularAlbedo[idx] = specularAlbedo;
    SpecularHitDistance[idx] = specHitDist;
#   endif
#endif    
}