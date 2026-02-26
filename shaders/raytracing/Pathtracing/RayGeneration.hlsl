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

    
#if defined(SHARC)
    SharcParameters sharcParameters = GetSharcParameters();

#    if defined(SHARC_UPDATE)
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
    
    Payload sourcePayload = TraceRayStandard(Scene, sourceRay, randomSeed);

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
#if defined(SHARC) && defined(SHARC_UPDATE)
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
        bool isEnter = true;
        
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

            // Update isEnter state when transmission occurs
            if (hasTransmission) {
                isEnter = !isEnter;
            } else {
                isEnter = dot(direction, faceNormalOriented) >= 0.0f;
            }

            brdfWeight.diffuse = bsdfSample.isLobe(LobeType::DiffuseReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
#   if defined(RAW_RADIANCE)
            brdfWeight.diffuse /= max(surface.DiffuseAlbedo, 1e-4f);
#   endif
            brdfWeight.specular = bsdfSample.isLobe(LobeType::SpecularReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            brdfWeight.transmission = bsdfSample.isLobe(LobeType::Transmission) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            
#   if defined(RAW_RADIANCE)
            float3 brdfWeightOriginal = brdfWeight.diffuse * surface.DiffuseAlbedo + brdfWeight.specular + brdfWeight.transmission;

#       if defined(SHARC) && defined(SHARC_UPDATE)
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
            
#if defined(SHARC) && defined(SHARC_UPDATE)
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
            
            if (isSpecular)
                specHitDist += payload.hitDistance;

            if (!payload.Hit())
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;

#if defined(SHARC) && defined(SHARC_UPDATE)
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#else
                sampleRadiance += skyIrradiance * throughput;
#endif                
                break;
            }
            
            float3 localPosition = ray.Origin + direction * payload.hitDistance;

            surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, material);

#if defined(SHARC)
            sharcHitData.positionWorld = surface.Position;
            sharcHitData.normalWorld = faceNormalOriented;

#   if SHARC_SEPARATE_EMISSIVE
            sharcHitData.emissive = surface.Emissive;
#   endif // SHARC_SEPARATE_EMISSIVE

#   if !defined(SHARC_UPDATE)
                uint gridLevel = HashGridGetLevel(surface.Position, sharcParameters.gridParameters);
                float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);
                bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);

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
            if (dot(surface.FaceNormal, brdfContext.ViewDirection) < 0.0f) surface.FlipNormal();

            AdjustShadingNormal(surface, brdfContext, true, false);  // Adjusts the normal of the supplied shading frame to reduce black pixels due to back-facing view direction.
            bsdf = StandardBSDF::make(surface, isEnter);

            float3 directRadiance = 0.0f;
#ifdef SUBSURFACE_SCATTERING
            if (surface.SubsurfaceData.HasSubsurface != 0 && !isSssPath) {
                directRadiance += EvaluateSubsurfaceNEE(surface, brdfContext, material, instance, payload, rayCone, randomSeed);
                isSssPath = true;
            }
            else
#endif
                directRadiance += EvaluateDirectRadiance(material, surface, brdfContext, instance, bsdf, randomSeed);
            sampleRadiance += directRadiance * throughput;

#if defined(SHARC) && defined(SHARC_UPDATE)
            if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, directRadiance, Random(randomSeed)))
                return;

            throughput = float3(1.0f, 1.0f, 1.0f);
#else
            sampleRadiance += surface.Emissive * throughput;
#endif
        }

        radiance += sampleRadiance;

#if defined(SHARC) && defined(SHARC_UPDATE)
        return;
#endif
    }

    radiance /= MAX_SAMPLES;        
    
    //const float2 envBRDF = BRDF::EnvBRDFApproxHirvonen(sourceSurface.Roughness, sourceBRDFContext.NdotV);
    //const float3 specularAlbedo = float3(sourceSurface.F0 * envBRDF.x + envBRDF.y);

    Output[idx] = float4(LLTrueLinearToGamma(direct + radiance), 1.0f);
}