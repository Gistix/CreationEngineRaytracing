#include "raytracing/GlobalIllumination/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"

#include "raytracing/include/Materials/TexLODHelpers.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "include/Lighting.hlsli"

#include "raytracing/include/Transparency.hlsli"

#if defined(SHARC)
#   include "Raytracing/Include/SHARC/Sharc.hlsli"
#   include "Raytracing/Include/SHARC/SHaRCHelper.hlsli"
#endif

#if defined(GROUP_TILING)
#   define DXC_STATIC_DISPATCH_GRID_DIM 1
#   include "include/ThreadGroupTilingX.hlsli"
#endif

#if defined(NRD)
#   include "include/NRD.hlsli"
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
    uint2 size = (uint2)max(uint2(1, 1), (uint2)ceil(float2(Camera.RenderSize) * Raytracing.ResolutionScale));  
#   if defined(GROUP_TILING)    
    uint2 idx = ThreadGroupTilingX((size + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE, THREAD_GROUP_SIZE.xx, 32, GTid.xy, Gid.xy);
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
        uint startIndex = Hash(idx);

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
    // This has to map to the original engine resolution (which may have its own resolution scale applied)
    const float2 dynamicUV = (float2(idx + 0.5f) / Camera.ScreenSize) / Raytracing.ResolutionScale;
    
    const float2 dynamicUVUnjittered = dynamicUV - (Camera.Jitter / Camera.ScreenSize);

    const float depth = Depth.SampleLevel(DefaultSampler, dynamicUVUnjittered, 0);

    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);

#if defined(WATER_INDIRECT_LIGHTING)
    // Water fill: transparent water is omitted from the game GBuffer, so fall
    // back to the rasterized water targets when water is nearer than the game
    // depth (covers both game-sky holes and water-over-opaque surfaces).
    const float waterDepth = WaterDepth.SampleLevel(DefaultSampler, dynamicUVUnjittered, 0);

    bool isWater = false;
    float waterDepthVS = 0.0f;
    [branch]
    if (waterDepth < SKY_Z)
    {
        waterDepthVS = ScreenToViewDepth(waterDepth, Camera.CameraData);
        if (waterDepthVS >= FP_VIEW_Z && waterDepth <= depth + 1e-4f)
        {
            isWater = true;
        }
    }

    [branch]
    if ((depthVS < FP_VIEW_Z || depth >= SKY_Z) && !isWater)
#else
    [branch]
    if (depthVS < FP_VIEW_Z || depth >= SKY_Z)
#endif
    {
#if !(defined(SHARC) && SHARC_UPDATE)
#   if defined(RAW_RADIANCE)
#       if defined(NRD)
#           if defined(NRD_REBLUR)
        DiffuseOutput[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);
        SpecularOutput[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);          
#           else
        DiffuseOutput[idx] = RELAX_FrontEnd_PackRadianceAndHitDist(0.0f, 0.0f, false);
        SpecularOutput[idx] = RELAX_FrontEnd_PackRadianceAndHitDist(0.0f, 0.0f, false);          
#           endif
#       else
        DiffuseOutput[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        SpecularOutput[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);   
#       endif // NRD
        
#   else // RAW_RADIANCE
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#   endif // !RAW_RADIANCE
        
#endif
        return;
    }    
    
#if defined(WATER_INDIRECT_LIGHTING)
    // RG16 xy packing, z reconstructed (upper hemisphere). Roughness is a
    // constant 0 for mirror water, matching the raster path that no longer
    // stores it.
    const float2 waterNormalXY = WaterNormal.SampleLevel(DefaultSampler, dynamicUV, 0);
    const float3 waterNormalWS = float3(waterNormalXY, sqrt(saturate(1.0f - dot(waterNormalXY, waterNormalXY))));
#endif

    const float4 normalRoughness = (float4)NormalRoughness.SampleLevel(DefaultSampler, dynamicUV, 0);
    const unorm float roughness = saturate(normalRoughness.w);
    const float3 metalnessAO = (float3)VAOMAO.SampleLevel(DefaultSampler, dynamicUV, 0);
    float3 faceNormal = normalize(FaceNormals.SampleLevel(DefaultSampler, dynamicUV, 0) * 2.0f - 1.0f);

    const unorm float metalness = saturate(metalnessAO.y);
    const unorm float ao = saturate(1.0f - metalnessAO.z);

    const float3 positionVS = ScreenToViewPosition(uv, depthVS, Camera.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Camera.ViewInverse);
    const float3 positionWS = positionCS + Camera.Position.xyz;    
    
    const float hitDistance = length(positionCS);
    
    const snorm float3 normalWS = normalRoughness.xyz;

    float3 tangentWS, bitangentWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangentWS);    

    float3 albedo = LLGammaToTrueLinear(Albedo.SampleLevel(DefaultSampler, dynamicUV, 0).rgb);

    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * hitDistance, Raytracing.PixelConeSpreadAngle);
    
    LightingMaterialData sourceMaterial = (LightingMaterialData)0;
    sourceMaterial.Feature = Feature::kDefault;
    
    Surface sourceSurface = SurfaceMaker::make(positionWS, faceNormal, normalWS, tangentWS, bitangentWS, albedo, roughness, metalness, 0, ao);
    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -positionCS / hitDistance);

    AdjustShadingNormal(sourceSurface, sourceBRDFContext, false, false);    

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, sourceSurface.Normal, sourceBRDFContext.ViewDirection, true);     

#if defined(WATER_INDIRECT_LIGHTING)
    Surface waterSurface = (Surface)0;
    BRDFContext waterBRDFContext = (BRDFContext)0;
    StandardBSDF waterBSDF = (StandardBSDF)0;
    RayCone waterRayCone = (RayCone)0;

    [branch]
    if (isWater)
    {
        const float waterDepthVS = ScreenToViewDepth(waterDepth, Camera.CameraData);
        const float3 waterPosVS = ScreenToViewPosition(uv, waterDepthVS, Camera.NDCToView);
        const float3 waterPosCS = ViewToWorldPosition(waterPosVS, Camera.ViewInverse);
        const float3 waterPosWS = waterPosCS + Camera.Position.xyz;
        const float waterHitDistance = length(waterPosCS);

        float3 waterTangentWS, waterBitangentWS;
        CreateOrthonormalBasis(waterNormalWS, waterTangentWS, waterBitangentWS);

        waterRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * waterHitDistance, Raytracing.PixelConeSpreadAngle);
        waterSurface = SurfaceMaker::make(waterPosWS, float3(0.0f, 0.0f, 1.0f), waterNormalWS, waterTangentWS, waterBitangentWS, float3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f, 0, 1.0f);
        waterBRDFContext = BRDFContext::make(waterSurface, -waterPosCS / waterHitDistance);
        AdjustShadingNormal(waterSurface, waterBRDFContext, false, false);
        waterBSDF = StandardBSDF::make(waterSurface, waterSurface.Normal, waterBRDFContext.ViewDirection, true);
    }
#endif

    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);   

#if defined(NRD)
    float3 rcpDiffFactor;
    float3 rcpSpecFactor;
    {
        float3 diffFactor, specFactor;
        NRD_MaterialFactors(sourceSurface.Normal, sourceBRDFContext.ViewDirection, sourceSurface.DiffuseAlbedo, sourceSurface.F0, sourceSurface.Roughness, diffFactor, specFactor);
        rcpDiffFactor = 1.0f / max(diffFactor, (float3)1e-4f);
        rcpSpecFactor = 1.0f / specFactor;

#   if defined(WATER_INDIRECT_LIGHTING)
        if (isWater)
        {
            float3 unusedDiffFactor;
            NRD_MaterialFactors(waterSurface.Normal, waterBRDFContext.ViewDirection, waterSurface.DiffuseAlbedo, waterSurface.F0, waterSurface.Roughness, unusedDiffFactor, specFactor);
            rcpSpecFactor = 1.0f / specFactor;
        }
#   endif
    }

#   if defined(NRD_REBLUR)
    const float diffHitDistNormScale = 1.0f / max(_REBLUR_GetHitDistanceNormalization(depthVS, Raytracing.HitDistSettings.xyz, sourceSurface.Roughness), 1e-6f);
#   endif
#endif

    half3 direction;
#if defined(RAW_RADIANCE) && !defined(NRD)
    MonteCarlo::BRDFWeight brdfWeight;
#endif

#if defined(RAW_RADIANCE)
    half3 diffuseRadiance = half3(0.0f, 0.0f, 0.0f);
    half3 specularRadiance = half3(0.0f, 0.0f, 0.0f);
#else
    half3 radiance = half3(0.0f, 0.0f, 0.0f);
#endif

    RayDesc ray;
    Payload payload;

    Instance instance;
    LightingMaterialData material;

    Surface surface;
    BRDFContext brdfContext;

    StandardBSDF bsdf;
    
    RayCone rayCone;    
    
#if defined(SHARC)
    SharcState sharcState;
    SharcHitData sharcHitData;
#endif    
    
#if defined(NRD)
    float diffHitDist = 0.0f;
    float specHitDist = 0.0f;
#   if MAX_SAMPLES > 1
    uint diffPathNum = 0;
    uint specPathNum = 0;
    specHitDist = NRD_FrontEnd_SpecHitDistAveraging_Begin();   
#   endif
#else
    uint diffPathNum = 0;
    uint specPathNum = 0;
#endif
    
    [loop]
    for (uint i = 0; i < MAX_SAMPLES; i++)
    {
        const uint sampleIndex = Camera.FrameIndex * MAX_SAMPLES + i;
        randomSeed = InitRandomSeed(idx, size, sampleIndex);

#if defined(SHARC) && SHARC_UPDATE
        SharcInit(sharcState);
#endif
        
#if defined(WATER_INDIRECT_LIGHTING)
        bool forceWaterSpecular = false;
        if (isWater)
        {
#if MAX_SAMPLES > 1
            forceWaterSpecular = (i % 2 == 0);
#else
            float NdotV = saturate(dot(waterSurface.Normal, waterBRDFContext.ViewDirection));
            float F = 0.02f + (1.0f - 0.02f) * pow(saturate(1.0f - NdotV), 5.0f);
            forceWaterSpecular = (Random(randomSeed) < clamp(F, 0.05f, 0.95f));
#endif
        }

        if (forceWaterSpecular)
        {
            surface = waterSurface;
            brdfContext = waterBRDFContext;
            bsdf = waterBSDF;
            rayCone = waterRayCone;
        }
        else
#endif
        {
            surface = sourceSurface;
            brdfContext = sourceBRDFContext;
            bsdf = sourceBSDF;
            rayCone = sourceRayCone;
        }

#if defined(NRD)
        float accumulatedHitDist = 0;
#endif

        material = sourceMaterial;
        
        half3 sampleRadiance = half3(0.0f, 0.0f, 0.0f);
        half3 throughput = half3(1.0f, 1.0f, 1.0f);
#if defined(SHARC)
        float materialRoughnessPrev = 0.0f;
#endif
        //bool isEnter = true;
        bool isSpecularSample = false;
        uint diffuseBounceCount = 0;

        // Throughput difference of demodulated first bounce
#if defined(RAW_RADIANCE) && !defined(NRD)
        float3 originalThroughput = float3(1.0f, 1.0f, 1.0f);
#endif            
        
        [loop]
        for (uint j = 0; j < MAX_BOUNCES; j++)
        {
            BSDFSample bsdfSample;
                              
            float3 faceNormalOriented = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;            
       
            float4 scatterSamples;
            float2 scatterExtraSamples;
            GenerateScatterBSDFSamples(idx, sampleIndex, j + 1, diffuseBounceCount, scatterSamples, scatterExtraSamples);
            if (bsdf.SampleBSDF(brdfContext, material.Feature, surface, bsdfSample, scatterSamples, scatterExtraSamples))
                direction = (half3)bsdfSample.wo;
            else
                break;            
 
#if defined(RAW_RADIANCE)
            if (j == 0)
                isSpecularSample = bsdfSample.isLobe(LobeType::Specular) || bsdfSample.isLobe(LobeType::Delta);
#endif
            if (bsdfSample.isLobe(LobeType::Diffuse))
                diffuseBounceCount++;
            
#if defined(RAW_RADIANCE) && !defined(NRD)
            const bool demodulatedThroughput = (j == 0 && !isSpecularSample);
#endif
            
            const bool hasTransmission = bsdfSample.isLobe(LobeType::Transmission);
            throughput *= half(hasTransmission || !bsdfSample.isLobe(LobeType::DiffuseReflection) ? 1.f : surface.AO);

#if defined(RAW_RADIANCE) && !defined(NRD)
            brdfWeight.diffuse = bsdfSample.isLobe(LobeType::DiffuseReflection) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            brdfWeight.specular = (bsdfSample.isLobe(LobeType::SpecularReflection) || bsdfSample.isLobe(LobeType::DeltaReflection)) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);
            brdfWeight.transmission = bsdfSample.isLobe(LobeType::Transmission) ? bsdfSample.weight : float3(0.f, 0.f, 0.f);

            if (demodulatedThroughput) {
                originalThroughput = throughput * bsdfSample.weight;

                float3 diffuseWeightDemodulated = all(surface.DiffuseAlbedo > 0.f)
                    ? brdfWeight.diffuse / max(surface.DiffuseAlbedo, 1e-4f)
                    : brdfWeight.diffuse;            
        
                throughput *= (half3)(diffuseWeightDemodulated + brdfWeight.specular + brdfWeight.transmission);                      
            } else {
                throughput *= (half3)bsdfSample.weight;
            }         
#else    // RAW_RADIANCE
            throughput *= (half3) bsdfSample.weight;
#endif   // !RAW_RADIANCE
            
#if defined(SHARC) && SHARC_UPDATE
            SharcSetThroughput(sharcState, throughput);
#else
#   if RUSSIAN_ROULETTE != 0          
#       if defined(RAW_RADIANCE) && !defined(NRD)
            // Apply russian roulette based on the original throughput
            float3 throughputColor = demodulatedThroughput ? originalThroughput : throughput;
    #       else
            float3 throughputColor = throughput;
#       endif            
#   endif
            
#   if RUSSIAN_ROULETTE == 1
            [branch]
            if (j > 0)
            {
                const float rrVal = 1.0f - min(1.0f, Color::RGBToLuminance(throughputColor));              
                const float rrProb = min(rrVal, 0.95f);

                if (Random(randomSeed) < rrProb)
                    break;

                throughput /= half(1.0f - rrProb);
            }
#   elif RUSSIAN_ROULETTE == 2
            const float rrVal = sqrt(Color::RGBToLuminance(throughputColor));
            float rrProb = saturate(0.85 - rrVal);
            rrProb *= rrProb;

            rrProb = saturate(rrProb + max(0, ((float)j / (float)MAX_BOUNCES - 0.4f)));

            if (Random(randomSeed) < rrProb)
                break;

            throughput /= half(1.0f - rrProb);
                
#       if defined(RAW_RADIANCE) && !defined(NRD)
            if (demodulatedThroughput)
                originalThroughput /= half(1.0f - rrProb);
#       endif                
#   endif
#endif
            
#if defined(SHARC) && (SHARC_ENABLE_SH_ENCODING || !SHARC_UPDATE)
            materialRoughnessPrev += bsdfSample.isLobe(LobeType::Diffuse) ? 1.0f : surface.Roughness;
#endif
            ray.Origin = surface.Position;

            ray.Direction = direction;
            ray.TMin = 0.1f;
            ray.TMax = RAY_TMAX;

            if (!bsdfSample.isLobe(LobeType::Delta))
                rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(bsdfSample.pdf), 2.0 * K_PI));

            payload = TraceRayStandard(Scene, ray, randomSeed);
            
            rayCone = rayCone.propagateDistance(payload.hitDistance);
        
            if (!payload.Hit())
            {
                half3 skyIrradiance = half3(SampleSky(SkyHemisphere, direction) * Raytracing.Sky);

#if defined(SHARC) && SHARC_UPDATE
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#else
                sampleRadiance += skyIrradiance * throughput;
#endif                
                break;
            }
            
#if defined(NRD)
            if (j == 0)
                accumulatedHitDist = payload.hitDistance;
#endif                      
            
            float3 localPosition = ray.Origin + direction * payload.hitDistance;

            surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, material, false);

#if defined(SHARC)
            sharcHitData.positionWorld = surface.Position;
            sharcHitData.normalWorld = surface.GeomNormal;

#   if SHARC_ENABLE_SH_ENCODING
            sharcHitData.radianceDirectionWorld = -direction;
            sharcHitData.radianceDirectionWeight = saturate(1.0f - materialRoughnessPrev);
#   endif // SHARC_ENABLE_SH_ENCODING

#   if SHARC_SEPARATE_EMISSIVE
            sharcHitData.emissive = surface.Emissive;
#   endif // SHARC_SEPARATE_EMISSIVE

#   if !SHARC_UPDATE
            uint gridLevel = HashGridGetLevel(surface.Position, sharcParameters.hashGridParameters);
            float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.hashGridParameters);
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
            /*isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
            if (!isEnter) {
                surface.FlipNormal();
                brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
            }*/

            AdjustShadingNormal(surface, brdfContext, true, false);  // Adjusts the normal of the supplied shading frame to reduce black pixels due to back-facing view direction.
            bsdf = StandardBSDF::make(surface, surface.Normal, brdfContext.ViewDirection, false);

            // Direct lighting
            half3 directRadiance = (half3)EvaluateDirectRadiance(material.Type, material.Feature, surface, brdfContext, instance, bsdf, randomSeed, false);
   
#if !(defined(SHARC) && SHARC_UPDATE)
            sampleRadiance += directRadiance * throughput;
#endif

#if defined(SHARC) && SHARC_UPDATE
            if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, directRadiance, Random(randomSeed)))
                return;

            throughput = half3(1.0f, 1.0f, 1.0f);
#else
            sampleRadiance += (half3)surface.Emissive * throughput;
#endif
            
#   if defined(RAW_RADIANCE) && !defined(NRD)
            // After throughput is applied to radiance, restore original throughput so that subsequent bounces is not increased due to missing diffuse albedo multiplication
            // This ensures first bounce has low frequency, allowing denoisers and linear upscaling to work with less per-pixel data
            // Diffuse albedo is re-applied during compositing (DiffuseRadiance * DiffuseAlbedo + SpecularRadiance)            
            if (demodulatedThroughput)
                throughput = originalThroughput;
#   endif    // RAW_RADIANCE     
        
        }

#if defined(NRD)
#   if defined(NRD_REBLUR)
        float normHitDist;
        if (isSpecularSample)
        {
#if defined(WATER_INDIRECT_LIGHTING)
            const float refDepthVS = isWater ? waterDepthVS : depthVS;
            const float refRoughness = isWater ? 0.0f : surface.Roughness;
#else
            const float refDepthVS = depthVS;
            const float refRoughness = surface.Roughness;
#endif
            normHitDist = REBLUR_FrontEnd_GetNormHitDist(accumulatedHitDist, refDepthVS, Raytracing.HitDistSettings.xyz, refRoughness);
        }
        else
        {
            normHitDist = max(saturate(accumulatedHitDist * diffHitDistNormScale), NRD_EPS);
        }
#   else
        float normHitDist = accumulatedHitDist;
#   endif
        
#   if MAX_SAMPLES > 1
        if (isSpecularSample) {
            NRD_FrontEnd_SpecHitDistAveraging_Add(specHitDist, normHitDist);        
        } else {
            diffHitDist += normHitDist;
            diffPathNum++;
        }
#   else
        if (isSpecularSample) {
            specHitDist = normHitDist;
        } else {
            diffHitDist = normHitDist;
        }
#   endif
#endif
        
#if defined(RAW_RADIANCE)
        if (isSpecularSample) {
            specularRadiance += sampleRadiance;
#   if MAX_SAMPLES > 1
            specPathNum++;
#   endif
        } else {
            diffuseRadiance += sampleRadiance;
#   if MAX_SAMPLES > 1
            diffPathNum++;
#   endif
        }
#else
        radiance += sampleRadiance;
#endif

#if defined(SHARC) && SHARC_UPDATE
        return;
#endif
    }

#if defined(NRD) && MAX_SAMPLES > 1
    NRD_FrontEnd_SpecHitDistAveraging_End(specHitDist);
    diffHitDist *= diffPathNum > 0 ? 1.0f / float(diffPathNum) : 0.0f;
#endif
    
#if defined(RAW_RADIANCE)
#   if MAX_SAMPLES > 1
#if defined(WATER_INDIRECT_LIGHTING)
    if (isWater)
    {
        diffuseRadiance *= diffPathNum > 0 ? (1.0f / float(diffPathNum)) : 0.0f;
        specularRadiance *= specPathNum > 0 ? (1.0f / float(specPathNum)) : 0.0f;
    }
    else
#endif
    {
        diffuseRadiance /= MAX_SAMPLES;
        specularRadiance /= MAX_SAMPLES;
    }
#   endif
#else
    radiance /= MAX_SAMPLES;
#endif
       
#if !(defined(SHARC) && SHARC_UPDATE)
#   if defined(RAW_RADIANCE)
#       if defined(NRD)
    diffuseRadiance *= (half3)rcpDiffFactor;
    specularRadiance *= (half3)rcpSpecFactor;
    
#   if defined(NRD_REBLUR)
    DiffuseOutput[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diffuseRadiance, diffHitDist, false);
    SpecularOutput[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(specularRadiance, specHitDist, false);  
#   else
    DiffuseOutput[idx] = RELAX_FrontEnd_PackRadianceAndHitDist(diffuseRadiance, diffHitDist, false);
    SpecularOutput[idx] = RELAX_FrontEnd_PackRadianceAndHitDist(specularRadiance, specHitDist, false);  
#   endif  
    
#       else // !NRD
    DiffuseOutput[idx] = float4(diffuseRadiance, diffHitDist);
    SpecularOutput[idx] = float4(specularRadiance, specHitDist);    
#       endif // NRD
    
#   else // !RAW_RADIANCE
    Output[idx] = float4(radiance, 1.0f);
#   endif // RAW_RADIANCE
#endif    
}
