#if !(defined(SHARC) && SHARC_UPDATE) && DEBUG_TRACE_HEATMAP
#   define NV_SHADER_EXTN_SLOT u127
#   define NV_SHADER_EXTN_REGISTER_SPACE space0
#   include "include/nvapi/nvHLSLExtns.h"

#   include "include/nvapi/Profiling.hlsli"
#endif

#include "raytracing/Pathtracing/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "include/Lighting.hlsli"

#if defined(SUBSURFACE_SCATTERING)
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
    
    RayDesc sourceRay = SetupPrimaryRay(idx, size, Camera);
    
    const float3 sourceDirection = sourceRay.Direction;
    
    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);
    
#if !(defined(SHARC) && SHARC_UPDATE) && DEBUG_TRACE_HEATMAP       
    uint startTime = NvGetSpecial( NV_SPECIALOP_GLOBAL_TIMER_LO );
#endif
    
    Payload sourcePayload = TraceRayStandard(Scene, sourceRay, randomSeed);

#if !(defined(SHARC) && SHARC_UPDATE) && DEBUG_TRACE_HEATMAP       
    uint endTime = NvGetSpecial( NV_SPECIALOP_GLOBAL_TIMER_LO );
    uint deltaTime = timediff(startTime, endTime);
    
    // Scale the time delta value to [0,1]
    static float heatmapScale = 300000.0f; // somewhat arbitrary scaling factor, experiment to find a value that works well in your app 
    float deltaTimeScaled =  clamp( (float)deltaTime / heatmapScale, 0.0f, 1.0f );

    // Compute the heatmap color and write it to the output pixel
    Output[idx] = float4(temperature(deltaTimeScaled), 1.0f);     
    
    return;
 #endif
    
    if (!sourcePayload.Hit())
    {
#if !(defined(SHARC) && SHARC_UPDATE)
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);   
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);    
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularHitDistance[idx] = RAY_TMAX;            
#endif     
        return;
    }
          
    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * sourcePayload.hitDistance, Raytracing.PixelConeSpreadAngle);   
    
    float3 sourcePosition = Camera.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
    Instance sourceInstance;
    Material sourceMaterial;

    Surface sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial, true);

    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -sourceDirection);

    bool sourceIsEnter = dot(sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection) >= 0.0f;
    if (!sourceIsEnter) 
        sourceSurface.FlipNormal();

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, sourceIsEnter);    
    
    AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);    
    
 #if !(defined(SHARC) && SHARC_UPDATE)
    DiffuseAlbedo[idx] = sourceSurface.DiffuseAlbedo;   
    const float2 envBRDF = BRDF::EnvBRDF(sourceSurface.Roughness, sourceBRDFContext.NdotV);
    SpecularAlbedo[idx] = float3(sourceSurface.F0 * envBRDF.x + envBRDF.y);
    NormalRoughness[idx] = float4(sourceSurface.Normal, sourceSurface.Roughness);   
#endif       
    
    bool isSssPath = false;
    
    float3 direct = sourceSurface.Emissive;
    
 #if defined(SHARC) && SHARC_DEBUG
    HashGridParameters gridParameters = GetSharcGridParameters();

    Output[idx] = float4(HashGridDebugColoredHash(sourceSurface.Position, sourceSurface.GeomNormal, gridParameters), 1);
    return;
#endif     
    
#if defined(SUBSURFACE_SCATTERING)
    if (sourceSurface.SubsurfaceData.HasSubsurface != 0) {
        direct += EvaluateSubsurfaceDiffuseNEE(sourceSurface, sourceBRDFContext, sourceMaterial, sourceInstance, sourcePayload, sourceRayCone, randomSeed, true);
        isSssPath = true;
        // Specular uses the standard path with diffuse suppressed
        Surface specSurface = sourceSurface;
        specSurface.DiffuseAlbedo = 0;
        StandardBSDF specBsdf = StandardBSDF::make(specSurface, true);
        direct += EvaluateDirectRadiance(sourceMaterial, specSurface, sourceBRDFContext, sourceInstance, specBsdf, randomSeed, false);
    }
    else
#endif
        direct += EvaluateDirectRadiance(sourceMaterial, sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, false);      
    
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
        
        material = sourceMaterial;
        instance = sourceInstance;
        payload = sourcePayload;
        
        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);
        float materialRoughnessPrev = 0.0f;
        bool isEnter = sourceIsEnter;

        // Water volume tracking for Beer-Lambert absorption
        bool insideWaterVolume = Camera.IsUnderwater != 0;
        float3 waterVolumeAbsorption = (insideWaterVolume && any(sourceSurface.VolumeAbsorption > 0.0f)) ? sourceSurface.VolumeAbsorption : float3(0.0f, 0.0f, 0.0f);
        
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
            isSpecular = bsdfSample.isLobe(LobeType::Specular);
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
            brdfWeight.specular = bsdfSample.isLobe(LobeType::SpecularReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
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

            float3 directRadiance = 0.0f;
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
    // Apply primary ray water absorption when camera is underwater
    if (Camera.IsUnderwater != 0 && any(sourceSurface.VolumeAbsorption > 0.0f))
    {
        float3 primaryWaterAttenuation = exp(-sourceSurface.VolumeAbsorption * sourcePayload.hitDistance);
        direct *= primaryWaterAttenuation;
        radiance *= primaryWaterAttenuation;
    }
    Output[idx] = float4(LLTrueLinearToGamma(direct + radiance), 1.0f);
    SpecularHitDistance[idx] = specHitDist;     
#endif    
}