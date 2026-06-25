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

#if defined(STABLE_PLANES)
#include "raytracing/include/PathTracerStablePlanes.hlsli"
#endif

#if defined(GROUP_TILING)
#   define DXC_STATIC_DISPATCH_GRID_DIM 1
#   include "include/ThreadGroupTilingX.hlsli"
#endif

#include "include/NRD.hlsli"

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

    // ReSTIR GI: Write empty packed surface (overwritten below on valid hit).
    // BUILD mode does not write (FILL follows immediately and writes the real data).
#if defined(RESTIR_GI)    
#   if !(defined(SHARC) && SHARC_UPDATE) && (PATH_TRACER_MODE != PATH_TRACER_MODE_BUILD_STABLE_PLANES)
    uint surfBufIdx = (Camera.FrameIndex % 2) * (size.x * size.y) + idx.y * size.x + idx.x;
    SurfaceDataBuffer[surfBufIdx] = PSD_Empty();
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
    
    // =========================================================================
    // Initialize StablePlanesContext (shared by BUILD and FILL)
    // =========================================================================
#if PATH_TRACER_MODE != PATH_TRACER_MODE_REFERENCE
    StablePlanesContext spCtx = StablePlanesContext::make(
        StablePlanesHeaderUAV, StablePlanesBufferUAV, StableRadianceUAV,
        size.x, size.y, cStablePlaneCount, cStablePlaneMaxVertexIndex);
#endif

    if (!sourcePayload.Hit())
    {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        // BUILD: initialize pixel and store sky miss for plane 0
        spCtx.StartPixel(idx);
        float3 skyRad = SampleSky(SkyHemisphere, sourceDirection) * Raytracing.Sky;
        float3x3 identityMat = float3x3(1,0,0, 0,1,0, 0,0,1);
        // Attenuate sky by water absorption when camera is underwater
        float3 buildMissThp = float3(1,1,1);
        if (Camera.IsUnderwater != 0 && any(Camera.UnderwaterAbsorption > 0.0f))
            buildMissThp *= exp(-Camera.UnderwaterAbsorption * kEnvironmentMapSceneDistance);
        StablePlanesHandleMiss(spCtx, idx, 0, 1, 1 /* sentinel branchID */,
            Camera.Position.xyz, sourceDirection, buildMissThp, float3(0,0,0),
            identityMat, skyRad, true);
        spCtx.StoreFirstHitRayLengthAndClearDominantToZero(idx, kEnvironmentMapSceneDistance);
        return;
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        // FILL: primary ray missed — output transparent like REFERENCE mode
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        MotionVectors[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        Depth[idx] = 1;  // sky → far plane (standard Z: 0=near, 1=far)
        
#   if defined(NRD) | defined(DLSS_RR)
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);
        
#       if defined(NRD) 
        ViewDepth[idx] = ScreenToViewDepth(1.0f, Camera.CameraData);
        
        DiffuseRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);
        SpecularRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);          
#       else
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);        
        SpecularHitDistance[idx] = RAY_TMAX;        
#       endif
#   endif
        return;
#else
        // REFERENCE: original behavior
#if !(defined(SHARC) && SHARC_UPDATE)
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        
#   if defined(NRD) | defined(DLSS_RR)
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f); 
        
#       if defined(NRD)
        DiffuseRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);
        SpecularRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(0.0f, 0.0f, false);          
#       else
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
        SpecularHitDistance[idx] = RAY_TMAX;                
#       endif              
#   endif       
        
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);

        float3 skyVirtualPos = sourceDirection * kEnvironmentMapSceneDistance;
        MotionVectors[idx] = float4(computeMotionVectorCameraRelative(
            skyVirtualPos,
            skyVirtualPos + (Camera.Position - Camera.PositionPrev)), 0);
        Depth[idx] = 1;  // sky → far plane (standard Z: 0=near, 1=far)    
#   if defined(NRD) 
        ViewDepth[idx] = ScreenToViewDepth(1.0f, Camera.CameraData);
#   endif        
#endif
        return;
#endif
    }
          
    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * sourcePayload.hitDistance, Raytracing.PixelConeSpreadAngle);   
    
    float3 sourcePosition = Camera.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
    Instance sourceInstance;
    LightingMaterialData sourceMaterial;

    Surface sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial, true);
    
 #if !(defined(SHARC) && SHARC_UPDATE)
    Output[idx] = float4(sourceSurface.Albedo, 1.0f);
    return;
 #endif
    
    // Pass through Effect materials on primary ray: accumulate emissive, don't interact
    float3 primaryEffectEmissive = float3(0, 0, 0);
#if defined(EFFECT_PASSTHROUGH)    
    [loop]
    for (uint effectPrimPass = 0; effectPrimPass < 16 && sourceMaterial.Type == Type::Effect; effectPrimPass++)
    {
        primaryEffectEmissive += sourceSurface.Emissive;

        float3 fn = dot(sourceDirection, sourceSurface.FaceNormal) <= 0.0f ? sourceSurface.FaceNormal : -sourceSurface.FaceNormal;
#if USE_SIA_INTERPOLATION
        sourceRay.Origin = OffsetRaySIA(sourceSurface.Position, fn, sourceSurface.SIAOffset, true);
#else
        sourceRay.Origin = OffsetRay(sourceSurface.Position, fn, sourceSurface.PositionError, true);
#endif
        sourceRay.Direction = sourceDirection;
        sourceRay.TMin = 0.0f;
        sourceRay.TMax = RAY_TMAX;

        sourcePayload = TraceRayStandard(Scene, sourceRay, randomSeed);

        if (!sourcePayload.Hit())
            break;

        sourcePosition = sourceRay.Origin + sourceDirection * sourcePayload.hitDistance;
        sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * length(sourcePosition - Camera.Position.xyz), Raytracing.PixelConeSpreadAngle);
        sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial, true);
    }

    // Effect pass-through ended in sky miss
    if (!sourcePayload.Hit())
    {
        float3 skyRadiance = SampleSky(SkyHemisphere, sourceDirection) * Raytracing.Sky + primaryEffectEmissive;
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        spCtx.StartPixel(idx);
        float3x3 identityMat = float3x3(1,0,0, 0,1,0, 0,0,1);
        float3 buildMissThp = float3(1,1,1);
        if (Camera.IsUnderwater != 0 && any(Camera.UnderwaterAbsorption > 0.0f))
            buildMissThp *= exp(-Camera.UnderwaterAbsorption * kEnvironmentMapSceneDistance);
        StablePlanesHandleMiss(spCtx, idx, 0, 1, 1,
            Camera.Position.xyz, sourceDirection, buildMissThp, float3(0,0,0),
            identityMat, skyRadiance, true);
        spCtx.StoreFirstHitRayLengthAndClearDominantToZero(idx, kEnvironmentMapSceneDistance);
        return;
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        Output[idx] = float4(LLTrueLinearToGamma(spCtx.GetAllRadiance(idx, true)), 1.0f);
        return;
#else
    #if !(defined(SHARC) && SHARC_UPDATE)
        Output[idx] = float4(LLTrueLinearToGamma(skyRadiance), 1.0f);
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        
        float3 skyVirtualPos = sourceDirection * kEnvironmentMapSceneDistance;
        MotionVectors[idx] = float4(computeMotionVectorCameraRelative(
            skyVirtualPos,
            skyVirtualPos + (Camera.Position - Camera.PositionPrev)), 0);
        Depth[idx] = 1;
    
#       if defined(NRD) | defined(DLSS_RR)   
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);
#           if defined(NRD) 
        ViewDepth[idx] = ScreenToViewDepth(1.0f, Camera.CameraData);
#           else
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
        SpecularHitDistance[idx] = RAY_TMAX;
#           endif  
#       endif
    
    #endif
        return;
#endif
    }
#endif
    
    float primarySceneDistance = length(sourcePosition - Camera.Position.xyz);

    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -sourceDirection);

    bool sourceIsEnter = dot(sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection) >= 0.0f;
    if (!sourceIsEnter) {
        sourceSurface.FlipNormal();
        sourceBRDFContext.NdotV = saturate(dot(sourceSurface.Normal, sourceBRDFContext.ViewDirection));
    }

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, sourceIsEnter);    
    
    AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);    
    
 #if !(defined(SHARC) && SHARC_UPDATE)
    // Coat-priority GBuffer: when coat is present, use coat normal/roughness for denoiser;
    // base diffuse is tinted by coat transmission (semi-transparent coat lets base color through).
    const bool useCoat = sourceSurface.CoatStrength > 0;
  
#   if defined(NRD) | defined(DLSS_RR)    
    const float3 coatTint = lerp(float3(1, 1, 1), sourceSurface.CoatColor, sourceSurface.CoatStrength);
    DiffuseAlbedo[idx] = sourceSurface.DiffuseAlbedo * coatTint;
#   endif   
    
#   if defined(DLSS_RR)    
    if (useCoat)
    {
        float coatNdotV = saturate(dot(sourceSurface.CoatNormal, sourceBRDFContext.ViewDirection));
        const float2 envBRDF = BRDF::EnvBRDF(sourceSurface.CoatRoughness, coatNdotV);
        SpecularAlbedo[idx] = float3(sourceSurface.CoatF0 * envBRDF.x + envBRDF.y);
    }
    else
    {
        const float2 envBRDF = BRDF::EnvBRDF(sourceSurface.Roughness, sourceBRDFContext.NdotV);
        SpecularAlbedo[idx] = float3(sourceSurface.F0 * envBRDF.x + envBRDF.y);
    }
#   endif
    
    NormalRoughness[idx] = float4(
        useCoat ? sourceSurface.CoatNormal : sourceSurface.Normal, 
        useCoat ? sourceSurface.CoatRoughness : sourceSurface.Roughness
    );
    
    // Write MV and Depth for REFERENCE mode (BUILD mode writes these in PathTracerStablePlanes)
#   if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    MotionVectors[idx] = float4(computeMotionVectorCameraRelative(
        sourceSurface.CameraRelativePosition,
        sourceSurface.PrevCameraRelativePosition), 0);
    
    const float depth = computeClipDepthCameraRelative(sourceSurface.CameraRelativePosition);
    Depth[idx] = depth;
    
#   if defined(NRD) 
    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    ViewDepth[idx] = depthVS;
#   endif
    
#       if defined(RESTIR_GI)    
    // Write packed surface data for ReSTIR GI (REFERENCE mode)
    SurfaceDataBuffer[surfBufIdx] = PSD_Pack(
        sourceSurface.Position, sourceSurface.Normal, sourceSurface.Tangent, sourceSurface.Bitangent,
        sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection,
        sourceSurface.DiffuseAlbedo, sourceSurface.F0,
        sourceSurface.Roughness, sourceSurface.Metallic,
        sourceMaterial.Feature, sourceSurface.SpecTrans > 0.0f,
        primarySceneDistance);
#       endif  
#   endif   
#endif   
    
    bool isSssPath = false;
    
    float3 direct = sourceSurface.Emissive + primaryEffectEmissive;
    
    // =========================================================================
    // BUILD MODE: Deterministic delta path exploration
    // =========================================================================
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    {
        spCtx.StartPixel(idx);
        spCtx.StoreFirstHitRayLengthAndClearDominantToZero(idx, primarySceneDistance);

        // Accumulate Effect emissive from primary pass-through into stable radiance
        if (any(primaryEffectEmissive > 0))
            spCtx.AccumulateStableRadiance(idx, primaryEffectEmissive);

        // Initial BUILD state for plane 0
        uint buildPlaneIndex = 0;
        uint buildVertexIndex = 1;          // camera=0, first hit=1
        uint buildBranchID = 1;             // sentinel bit
        float3 buildThp = float3(1,1,1);
        // Base MV for the primary surface. Deeper stable planes compute PSR MV from
        // their virtual path-space surface in StablePlanesHandleHit/Miss.
        float3 buildMVs = computeMotionVectorCameraRelative(
            sourceSurface.CameraRelativePosition,
            sourceSurface.PrevCameraRelativePosition);
        float buildSceneLength = primarySceneDistance;
        float3x3 buildImageXform = float3x3(1,0,0, 0,1,0, 0,0,1);
        float buildRoughnessAccum = 0;
        bool buildIsDominant = true;

        // Water volume tracking for BUILD pass (Beer-Lambert absorption along delta paths)
        bool buildInsideWater = Camera.IsUnderwater != 0;
        float3 buildWaterAbsorption = buildInsideWater ? Camera.UnderwaterAbsorption : float3(0.0f, 0.0f, 0.0f);

        // Apply primary ray water absorption
        if (buildInsideWater)
            buildThp *= exp(-buildWaterAbsorption * sourcePayload.hitDistance);

        // Handle the primary surface through StablePlanesHandleHit
        StablePlanesHitResult hitResult = StablePlanesHandleHit(
            spCtx, idx, buildPlaneIndex, buildVertexIndex, buildBranchID,
            Camera.Position.xyz, sourceDirection, sourcePayload.hitDistance,
            buildSceneLength, buildThp, buildMVs, buildImageXform, buildRoughnessAccum,
            sourceSurface, sourceBRDFContext, sourceBSDF, buildIsDominant,
            sourceInstance, randomSeed,
            buildInsideWater, buildWaterAbsorption);

        // When plane 0 stored a delta base (multi-fork), dominant status transfers to first child
        bool childNeedsDominant = buildIsDominant && !hitResult.continueTracing;

        // If delta-only surface, continue tracing the primary lobe
        while (hitResult.continueTracing)
        {
            RayDesc buildRay;
            buildRay.Origin = hitResult.nextRayOrigin;
            buildRay.Direction = hitResult.nextRayDir;
            buildRay.TMin = 0.0f;
            buildRay.TMax = RAY_TMAX;

            Payload buildPayload = TraceRayStandard(Scene, buildRay, randomSeed);
            buildSceneLength += buildPayload.hitDistance;

            // Apply water absorption for this delta path segment
            if (hitResult.nextInsideWater)
                hitResult.nextThp *= exp(-hitResult.nextWaterAbsorption * buildPayload.hitDistance);

            if (!buildPayload.Hit())
            {
                float3 skyRad = SampleSky(SkyHemisphere, hitResult.nextRayDir) * Raytracing.Sky;
                StablePlanesHandleMiss(spCtx, idx, buildPlaneIndex, hitResult.nextVertexIndex,
                    hitResult.nextBranchID, hitResult.nextRayOrigin, hitResult.nextRayDir,
                    hitResult.nextThp, buildMVs, hitResult.nextImageXform, skyRad, buildIsDominant);
                break;
            }

            float3 buildHitPos = buildRay.Origin + buildRay.Direction * buildPayload.hitDistance;
            Instance buildInstance;
            LightingMaterialData buildMaterial;
            RayCone buildRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * buildSceneLength, Raytracing.PixelConeSpreadAngle);
            Surface buildSurface = SurfaceMaker::make(buildHitPos, buildPayload, hitResult.nextRayDir, buildRayCone, buildInstance, buildMaterial, true);
            BRDFContext buildBrdfCtx = BRDFContext::make(buildSurface, -hitResult.nextRayDir);
            bool buildIsEnter = dot(buildSurface.FaceNormal, buildBrdfCtx.ViewDirection) >= 0.0f;
            if (!buildIsEnter) {
                buildSurface.FlipNormal();
                buildBrdfCtx.NdotV = saturate(dot(buildSurface.Normal, buildBrdfCtx.ViewDirection));
            }
            AdjustShadingNormal(buildSurface, buildBrdfCtx, true, false);
            StandardBSDF buildBsdf = StandardBSDF::make(buildSurface, buildIsEnter);

            hitResult = StablePlanesHandleHit(
                spCtx, idx, buildPlaneIndex, hitResult.nextVertexIndex, hitResult.nextBranchID,
                hitResult.nextRayOrigin, hitResult.nextRayDir, buildPayload.hitDistance,
                buildSceneLength, hitResult.nextThp, buildMVs, hitResult.nextImageXform,
                hitResult.nextRoughnessAccum, buildSurface, buildBrdfCtx, buildBsdf, buildIsDominant,
                buildInstance, randomSeed,
                hitResult.nextInsideWater, hitResult.nextWaterAbsorption);
        }

        if (buildIsDominant)
            childNeedsDominant = true;

        // Explore forked paths (planes 1, 2, ...)
        int nextExplorePlane = spCtx.FindNextToExplore(idx, 1);
        while (nextExplorePlane >= 0)
        {
            uint4 expPacked[5];
            spCtx.ExplorationStart(idx, nextExplorePlane, expPacked);
            StablePlaneExplorationPayload ep = StablePlaneExplorationPayload::Unpack(expPacked);

            buildPlaneIndex = nextExplorePlane;
            buildIsDominant = childNeedsDominant;
            childNeedsDominant = false; // only the first child becomes dominant

            float expSceneLength = ep.sceneLength;

            RayDesc expRay;
            expRay.Origin = ep.rayOrigin;
            expRay.Direction = ep.rayDir;
            expRay.TMin = 0.0f;
            expRay.TMax = RAY_TMAX;

            Payload expPayload = TraceRayStandard(Scene, expRay, randomSeed);
            expSceneLength += expPayload.hitDistance;

            // Apply water absorption for exploration segment
            if (ep.insideWaterVolume)
                ep.throughput *= exp(-ep.waterVolumeAbsorption * expPayload.hitDistance);

            if (!expPayload.Hit())
            {
                float3 skyRad = SampleSky(SkyHemisphere, ep.rayDir) * Raytracing.Sky;
                StablePlanesHandleMiss(spCtx, idx, buildPlaneIndex, ep.vertexIndex,
                    ep.stableBranchID, ep.rayOrigin, ep.rayDir, ep.throughput, ep.motionVectors,
                    ep.imageXform, skyRad, buildIsDominant);
            }
            else
            {
                float3 expHitPos = expRay.Origin + expRay.Direction * expPayload.hitDistance;
                Instance expInstance;
                LightingMaterialData expMaterial;
                RayCone expRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * expSceneLength, Raytracing.PixelConeSpreadAngle);
                Surface expSurface = SurfaceMaker::make(expHitPos, expPayload, ep.rayDir, expRayCone, expInstance, expMaterial, false);
                BRDFContext expBrdfCtx = BRDFContext::make(expSurface, -ep.rayDir);
                bool expIsEnter = dot(expSurface.FaceNormal, expBrdfCtx.ViewDirection) >= 0.0f;
                if (!expIsEnter) {
                    expSurface.FlipNormal();
                    expBrdfCtx.NdotV = saturate(dot(expSurface.Normal, expBrdfCtx.ViewDirection));
                }
                AdjustShadingNormal(expSurface, expBrdfCtx, true, false);
                StandardBSDF expBsdf = StandardBSDF::make(expSurface, expIsEnter);

                StablePlanesHitResult expHitResult = StablePlanesHandleHit(
                    spCtx, idx, buildPlaneIndex, ep.vertexIndex, ep.stableBranchID,
                    ep.rayOrigin, ep.rayDir, expPayload.hitDistance,
                    expSceneLength, ep.throughput, ep.motionVectors, ep.imageXform,
                    ep.roughnessAccum, expSurface, expBrdfCtx, expBsdf, buildIsDominant,
                    expInstance, randomSeed,
                    ep.insideWaterVolume, ep.waterVolumeAbsorption);

                while (expHitResult.continueTracing)
                {
                    RayDesc contRay;
                    contRay.Origin = expHitResult.nextRayOrigin;
                    contRay.Direction = expHitResult.nextRayDir;
                    contRay.TMin = 0.0f;
                    contRay.TMax = RAY_TMAX;

                    Payload contPayload = TraceRayStandard(Scene, contRay, randomSeed);
                    expSceneLength += contPayload.hitDistance;

                    // Apply water absorption for this continuation segment
                    if (expHitResult.nextInsideWater)
                        expHitResult.nextThp *= exp(-expHitResult.nextWaterAbsorption * contPayload.hitDistance);

                    if (!contPayload.Hit())
                    {
                        float3 skyRad2 = SampleSky(SkyHemisphere, expHitResult.nextRayDir) * Raytracing.Sky;
                        StablePlanesHandleMiss(spCtx, idx, buildPlaneIndex, expHitResult.nextVertexIndex,
                            expHitResult.nextBranchID, expHitResult.nextRayOrigin, expHitResult.nextRayDir,
                            expHitResult.nextThp, ep.motionVectors, expHitResult.nextImageXform, skyRad2, buildIsDominant);
                        break;
                    }

                    float3 contHitPos = contRay.Origin + contRay.Direction * contPayload.hitDistance;
                    Instance contInstance;
                    LightingMaterialData contMaterial;
                    RayCone contRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * expSceneLength, Raytracing.PixelConeSpreadAngle);
                    Surface contSurface = SurfaceMaker::make(contHitPos, contPayload, expHitResult.nextRayDir, contRayCone, contInstance, contMaterial, false);
                    BRDFContext contBrdfCtx = BRDFContext::make(contSurface, -expHitResult.nextRayDir);
                    bool contIsEnter = dot(contSurface.FaceNormal, contBrdfCtx.ViewDirection) >= 0.0f;
                    if (!contIsEnter) {
                        contSurface.FlipNormal();
                        contBrdfCtx.NdotV = saturate(dot(contSurface.Normal, contBrdfCtx.ViewDirection));
                    }
                    AdjustShadingNormal(contSurface, contBrdfCtx, true, false);
                    StandardBSDF contBsdf = StandardBSDF::make(contSurface, contIsEnter);

                    expHitResult = StablePlanesHandleHit(
                        spCtx, idx, buildPlaneIndex, expHitResult.nextVertexIndex, expHitResult.nextBranchID,
                        expHitResult.nextRayOrigin, expHitResult.nextRayDir, contPayload.hitDistance,
                        expSceneLength, expHitResult.nextThp, ep.motionVectors, expHitResult.nextImageXform,
                        expHitResult.nextRoughnessAccum, contSurface, contBrdfCtx, contBsdf, buildIsDominant,
                        contInstance, randomSeed,
                        expHitResult.nextInsideWater, expHitResult.nextWaterAbsorption);
                }
            }

            nextExplorePlane = spCtx.FindNextToExplore(idx, nextExplorePlane + 1);
        }

        return; // BUILD pass done
    }
#endif // BUILD

    // =========================================================================
    // FILL MODE: Restore path from stable plane buffer
    // =========================================================================
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
    StablePlaneFillState fillState;
    float4 fillPathL = float4(0,0,0,0);
    bool fillInsideWater = false;
    float3 fillWaterAbsorption = float3(0.0f, 0.0f, 0.0f);
    float3 fillPlaneThp = float3(1.0f, 1.0f, 1.0f); // plane's stored throughput, used as initial bounce throughput
    float fillSceneLength = 0;
    {
        float3 fillRayOrigin, fillRayDir, fillThp;
        uint fillVertexIndex;
        float2 fillTMinMax = FirstHitFromVBuffer(fillState, fillRayOrigin, fillRayDir, fillThp,
            fillSceneLength, fillVertexIndex, fillInsideWater, fillWaterAbsorption, spCtx, idx, 0);

        if (fillTMinMax.x < 0)
        {
            // VBuffer indicated a miss — output stable radiance only
            Output[idx] = float4(LLTrueLinearToGamma(spCtx.GetAllRadiance(idx, true)), 1.0f);
            return;
        }

        // Re-trace with narrow window to cheaply re-hit the same surface
        RayDesc fillRay;
        fillRay.Origin = fillRayOrigin;
        fillRay.Direction = fillRayDir;
        fillRay.TMin = fillTMinMax.x;
        fillRay.TMax = fillTMinMax.y;

        Payload fillPayload = TraceRayStandard(Scene, fillRay, randomSeed);

        if (!fillPayload.Hit())
        {
            Output[idx] = float4(LLTrueLinearToGamma(spCtx.GetAllRadiance(idx, true)), 1.0f);
            return;
        }

        // Reconstruct surface from re-traced hit — this becomes the "source" for the bounce loop
        float3 fillHitPos = fillRayOrigin + fillRayDir * fillPayload.hitDistance;
        sourcePayload = fillPayload;
        sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * fillSceneLength, Raytracing.PixelConeSpreadAngle);
        sourceSurface = SurfaceMaker::make(fillHitPos, fillPayload, fillRayDir, sourceRayCone, sourceInstance, sourceMaterial, true);
        sourceBRDFContext = BRDFContext::make(sourceSurface, -fillRayDir);
        sourceIsEnter = dot(sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection) >= 0.0f;
        if (!sourceIsEnter) {
            sourceSurface.FlipNormal();
            sourceBRDFContext.NdotV = saturate(dot(sourceSurface.Normal, sourceBRDFContext.ViewDirection));
        }
        AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);
        sourceBSDF = StandardBSDF::make(sourceSurface, sourceIsEnter);
        fillPlaneThp = fillThp;

        // SurfaceDataBuffer for ReSTIR GI is now written in the bounce loop
        // at the scattering surface just before the first non-delta BSDF sample,
        // so that delta surfaces (mirrors/glass) are skipped correctly.
    }

    // Update GBuffer with the stable plane's base surface data (used by DLSS-RR).
    // Diffuse/normal follow the dominant plane, but specular albedo stays on the first plane.
    // Coat-priority: when coat is present, dominant plane already stores coat-aware data from BUILD;
    // for plane 0, coat properties are applied directly.
    uint dominantPlane = spCtx.LoadDominantIndex(idx);
    if (dominantPlane != 0)
    {
        StablePlane domSP = spCtx.LoadStablePlane(idx, dominantPlane);
        float domRoughness = domSP.GetRoughness();
        float3 domNormal = domSP.GetNormal();
        float3 domDiffEst, domSpecEst;
        UnpackTwoFp32ToFp16(domSP.DenoiserPackedBSDFEstimate, domDiffEst, domSpecEst);
#   if defined(NRD) | defined(DLSS_RR)
        DiffuseAlbedo[idx] = domDiffEst;
#   endif
        NormalRoughness[idx] = float4(domNormal, domRoughness);
    }
    else
    {
        if (sourceSurface.CoatStrength > 0)
        {
            float3 coatTint = lerp(float3(1,1,1), sourceSurface.CoatColor, sourceSurface.CoatStrength);
#   if defined(NRD) | defined(DLSS_RR)
            DiffuseAlbedo[idx] = sourceSurface.DiffuseAlbedo * coatTint;
#   endif
            NormalRoughness[idx] = float4(sourceSurface.CoatNormal, sourceSurface.CoatRoughness);
        }
        else
        {
#   if defined(NRD) | defined(DLSS_RR)
            DiffuseAlbedo[idx] = sourceSurface.DiffuseAlbedo;
#   endif
            NormalRoughness[idx] = float4(sourceSurface.Normal, sourceSurface.Roughness);
        }
    }

#   if defined(DLSS_RR) 
    if (sourceSurface.CoatStrength > 0)
    {
        float coatNdotV = saturate(dot(sourceSurface.CoatNormal, sourceBRDFContext.ViewDirection));
        const float2 coatEnvBRDF = BRDF::EnvBRDF(sourceSurface.CoatRoughness, coatNdotV);
        SpecularAlbedo[idx] = float3(sourceSurface.CoatF0 * coatEnvBRDF.x + coatEnvBRDF.y);
    }
    else
    {
        const float2 envBRDF2 = BRDF::EnvBRDF(sourceSurface.Roughness, sourceBRDFContext.NdotV);
        SpecularAlbedo[idx] = float3(sourceSurface.F0 * envBRDF2.x + envBRDF2.y);
    }
#   endif
    
    // In FILL mode, emissive along delta paths was captured in BUILD → skip to avoid double-counting
    direct = 0;
#endif // FILL

 #if defined(SHARC) && SHARC_DEBUG
    HashGridParameters gridParameters = GetSharcGridParameters();

    Output[idx] = float4(HashGridDebugColoredHash(sourceSurface.Position, sourceSurface.GeomNormal, gridParameters), 1);
    return;
#endif     
    
    // Handle direct lighting, with special treatment for delta lobes.
    // For non-delta lobes: standard NEE (EvaluateDirectRadiance) evaluates BSDF at sampled light directions.
    // For delta lobes: EvalDeltaLobeLighting checks if delta reflection/refraction directions fall within
    // each light source's solid angle, providing correct mirror reflections of analytical lights.
    {
        const uint sourceLobes = sourceBSDF.GetLobes(sourceSurface);
        const bool sourceHasNonDeltaLobes = (sourceLobes & (uint)LobeType::NonDelta) != 0;
        const bool sourceHasDeltaLobes = (sourceLobes & (uint)LobeType::Delta) != 0;
        
        if (sourceHasNonDeltaLobes)
        {
#if defined(SUBSURFACE_SCATTERING)
            if (sourceSurface.SubsurfaceData.HasSubsurface != 0) {
                direct += EvaluateSubsurfaceDiffuseNEE(sourceSurface, sourceBRDFContext, sourceMaterial, sourceInstance, sourcePayload, sourceRayCone, randomSeed, true);
                isSssPath = true;
                // Specular uses the standard path with diffuse suppressed
                Surface specSurface = sourceSurface;
                specSurface.DiffuseAlbedo = 0;
                StandardBSDF specBsdf = StandardBSDF::make(specSurface, true);
                direct += EvaluateDirectRadiance(sourceMaterial.Type, sourceMaterial.Feature, specSurface, sourceBRDFContext, sourceInstance, specBsdf, randomSeed, true);
            }
            else
#endif
                direct += EvaluateDirectRadiance(sourceMaterial.Type, sourceMaterial.Feature, sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true);
        }
        
        // Delta lobe lighting: check if delta reflection/refraction directions see any analytical lights.
        // Skip for pure delta surfaces — their delta lighting was captured in BUILD's stable radiance.
        if (sourceHasDeltaLobes && sourceHasNonDeltaLobes)
        {
            direct += EvalDeltaLobeLighting(sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true);
        }
    }
    
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
    // Accumulate primary surface direct lighting with plane throughput baked in
    // (GetAllRadiance no longer multiplies by stored thp)
    if (any(direct > 0))
        fillPathL += float4(direct * fillPlaneThp, 0);
#endif
    
    float3 direction;
    MonteCarlo::BRDFWeight brdfWeight;

    float3 radiance = 0;
    bool isSpecular = false;
    bool isSpecularSample = false;
    
#if defined(NRD)
     float diffHitDist = 0;     
     float specHitDist = NRD_FrontEnd_SpecHitDistAveraging_Begin();   
#else
    float specHitDist = RAY_TMAX;
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
        
#if defined(NRD)
        float accumulatedHitDist = 0;
#endif        
        
        material = sourceMaterial;
        instance = sourceInstance;
        payload = sourcePayload;
        
        float3 sampleRadiance = float3(0.0f, 0.0f, 0.0f);
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        float3 throughput = fillPlaneThp; // start with plane's delta path throughput
#else
        float3 throughput = float3(1.0f, 1.0f, 1.0f);
#endif
        bool arrivedViaDelta = false;
        float materialRoughnessPrev = 0.0f;
        bool isEnter = sourceIsEnter;

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        // ReSTIR GI: multi-bounce secondary radiance accumulation
        float3 giSecRadiance = 0;
        float3 giSecThroughput = 0;
        float giSecPdf = 0;
        bool giSecStarted = false;
        Surface giScatterSurface = (Surface)0;
        float3 giScatterViewDir = 0;
        float3 giScatterThp = 0;
        uint giScatterFeature = Feature::kDefault;
        bool giScatterHasSpecularTransmission = false;
#endif

        // Water volume tracking for Beer-Lambert absorption
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        bool insideWaterVolume = fillInsideWater;
        float3 waterVolumeAbsorption = fillWaterAbsorption;
#else
        bool insideWaterVolume = Camera.IsUnderwater != 0;
        float3 waterVolumeAbsorption = insideWaterVolume ? Camera.UnderwaterAbsorption : float3(0.0f, 0.0f, 0.0f);
#endif
        
#if defined(RAW_RADIANCE)
        float3 throughputDelta = float3(1.0f, 1.0f, 1.0f);
#endif        
        
        [loop]
        for (uint j = 0; j < MAX_BOUNCES; j++)
        {
            BSDFSample bsdfSample;
            bool isPrimaryReplacement = false;
            
            float3 faceNormalOriented = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0f ? surface.FaceNormal : -surface.FaceNormal;            

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
            // ReSTIR GI: snapshot the current scattering surface for SurfaceDataBuffer.
            // Updated each iteration until giSecStarted; the final snapshot is the
            // surface from which the first non-delta ray was emitted.
            if (Raytracing.EnableReSTIRGI && !giSecStarted)
            {
                giScatterSurface = surface;
                giScatterViewDir = brdfContext.ViewDirection;
                giScatterThp = throughput;
                giScatterFeature = material.Feature;
                giScatterHasSpecularTransmission = surface.SpecTrans > 0.0f;
            }
#endif

#if LIGHTING_MODE == LIGHTING_MODE_DIFFUSE
            direction = surface.Mul(SampleCosineHemisphere(randomSeed));

            float NdotD = saturate(dot(surface.Normal, direction));

            throughput *= surface.AO;
            throughput *= surface.Albedo;
            
            const bool hasTransmission = false;
#else            
            bool isValid = bsdf.SampleBSDF(brdfContext, material.Feature, surface, bsdfSample, randomSeed);
            
            if (isValid)
                direction = bsdfSample.wo;
            else
                break;
            
            bool isDelta = bsdfSample.isLobe(LobeType::Delta);
            isSpecular = bsdfSample.isLobe(LobeType::Specular) || isDelta;
            
            if (j == 0)
                isSpecularSample = isSpecular;         
                  
            bool hasTransmission = bsdfSample.isLobe(LobeType::Transmission);
            isPrimaryReplacement = surface.Primary && isDelta;
            arrivedViaDelta = isDelta;

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
            
#if USE_SIA_INTERPOLATION
            ray.Origin = OffsetRaySIA(surface.Position, faceNormalOriented, surface.SIAOffset, hasTransmission);
#else
            ray.Origin = OffsetRay(surface.Position, faceNormalOriented, surface.PositionError, hasTransmission);
#endif
            ray.Direction = direction;
            ray.TMin = 0.0f;  // Offset already handles precision, no additional offset needed
            ray.TMax = RAY_TMAX;

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
            // Track stable plane branch after each scatter
            StablePlanesOnScatter(fillState, fillPathL, bsdfSample, j + 2, spCtx, idx);
#endif

            if (!bsdfSample.isLobe(LobeType::Delta))
                rayCone = RayCone::make(rayCone.getWidth(), min(rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF(bsdfSample.pdf), 2.0 * K_PI));

            payload = TraceRayStandard(Scene, ray, randomSeed);
            
            rayCone = rayCone.propagateDistance(payload.hitDistance);

            // Apply Beer-Lambert volume absorption for water
            if (insideWaterVolume)
            {
                throughput *= exp(-waterVolumeAbsorption * payload.hitDistance);
            }
            
#if defined(NRD)
            if (j == 0)
                accumulatedHitDist = payload.hitDistance;
#else
            if (isSpecularSample)
                specHitDist = min(specHitDist, payload.hitDistance);
#endif               
            
            if (!payload.Hit())
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;

#if defined(SHARC) && SHARC_UPDATE
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
                // In FILL mode: skip sky if on stable branch (already captured in BUILD)
                if (giSecStarted)
                {
                    // ReSTIR GI owns this radiance — divert, don't accumulate into fillPathL
                    float3 relTp = throughput / max(giSecThroughput, 1e-10);
                    giSecRadiance += skyIrradiance * relTp;
                }
                else if (!fillState.hasFlag(kStablePlaneFlag_OnBranch))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(skyIrradiance * throughput) : 0;
                    fillPathL += float4(skyIrradiance * throughput, specAvg);
                }
#else
                sampleRadiance += skyIrradiance * throughput;
#endif                
                break;
            }
            
            float3 localPosition = ray.Origin + direction * payload.hitDistance;

            surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, material, isPrimaryReplacement);

#if defined(EFFECT_PASSTHROUGH)         
            // Pass through Effect materials in bounce: accumulate emissive, continue ray unchanged
            bool effectMiss = false;
            [loop]
            for (uint effectBouncePass = 0; effectBouncePass < 16 && material.Type == Type::Effect; effectBouncePass++)
            {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
                if (!giSecStarted && !fillState.hasFlag(kStablePlaneFlag_OnBranch) && any(surface.Emissive > 0))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(surface.Emissive * throughput) : 0;
                    fillPathL += float4(surface.Emissive * throughput, specAvg);
                }
                // Effect emissive after giSecStarted is captured via the main giSecRadiance accumulation
#else
                sampleRadiance += surface.Emissive * throughput;
#endif
                float3 fn = dot(direction, surface.FaceNormal) <= 0.0f ? surface.FaceNormal : -surface.FaceNormal;
#if USE_SIA_INTERPOLATION
                ray.Origin = OffsetRaySIA(surface.Position, fn, surface.SIAOffset, true);
#else
                ray.Origin = OffsetRay(surface.Position, fn, surface.PositionError, true);
#endif
                ray.Direction = direction;
                ray.TMin = 0.0f;
                ray.TMax = RAY_TMAX;

                payload = TraceRayStandard(Scene, ray, randomSeed);
                rayCone = rayCone.propagateDistance(payload.hitDistance);

                if (insideWaterVolume)
                    throughput *= exp(-waterVolumeAbsorption * payload.hitDistance);

                if (!payload.Hit())
                {
                    effectMiss = true;
                    break;
                }

                localPosition = ray.Origin + direction * payload.hitDistance;
                surface = SurfaceMaker::make(localPosition, payload, direction, rayCone, instance, material, isPrimaryReplacement);
            }

            if (effectMiss)
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;
#if defined(SHARC) && SHARC_UPDATE
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
                if (giSecStarted)
                {
                    float3 relTp = throughput / max(giSecThroughput, 1e-10);
                    giSecRadiance += skyIrradiance * relTp;
                }
                else if (!fillState.hasFlag(kStablePlaneFlag_OnBranch))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(skyIrradiance * throughput) : 0;
                    fillPathL += float4(skyIrradiance * throughput, specAvg);
                }
#else
                sampleRadiance += skyIrradiance * throughput;
#endif
                break;
            }
#endif
            
            // ReSTIR GI: capture secondary surface geometry before SHaRC may terminate the path
#if defined(RESTIR_GI)              
#   if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES     
            if (Raytracing.EnableReSTIRGI && !giSecStarted && !arrivedViaDelta &&
                giScatterFeature != Feature::kHairTint && material.Feature != Feature::kHairTint &&
                !giScatterHasSpecularTransmission && surface.SpecTrans <= 0.0f)
            {
                // Write SurfaceDataBuffer with the scattering surface (primary for GI)
                SurfaceDataBuffer[surfBufIdx] = PSD_Pack(
                    giScatterSurface.Position, giScatterSurface.Normal,
                    giScatterSurface.Tangent, giScatterSurface.Bitangent,
                    giScatterSurface.FaceNormal, giScatterViewDir,
                    giScatterSurface.DiffuseAlbedo, giScatterSurface.F0,
                    giScatterSurface.Roughness, giScatterSurface.Metallic,
                    giScatterFeature, giScatterHasSpecularTransmission,
                    fillSceneLength);

                // Write secondary surface geometry (the hit surface)
                float3 captureNormal = dot(surface.FaceNormal, -direction) >= 0.0f ? surface.Normal : -surface.Normal;
                half2 encodedN = EncodeNormal((half3)captureNormal);
                uint packedNorm = f32tof16(encodedN.x) | (f32tof16(encodedN.y) << 16);
                SecondaryGBufPositionNormal[idx] = float4(surface.Position, asfloat(packedNorm));
                SecondaryGBufDiffuseAlbedo[idx] = float4(surface.DiffuseAlbedo, Color::RGBToLuminance(giScatterThp));
                SecondaryGBufSpecularRough[idx] = float4(surface.F0, surface.Roughness);
                giSecStarted = true;
                giSecThroughput = throughput;
                giSecPdf = bsdfSample.pdf;
            }
#   endif
#endif

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
            uint gridLevel = HashGridGetLevel(surface.Position, sharcParameters.gridParameters);
            float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);
            bool isValidHit = payload.hitDistance > voxelSize * sqrt(3.0f);
            
            const bool oldValidHit = isValidHit;
            
            if (isValidHit) {
                materialRoughnessPrev = min(materialRoughnessPrev, 0.99f);
                float a2 = materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev * materialRoughnessPrev;
                float footprint = payload.hitDistance * sqrt(0.5f * a2 / max(1.0f - a2, DIV_EPSILON));
                isValidHit &= footprint > voxelSize * M_TO_GAME_UNIT;
                isValidHit &= material.Feature != Feature::kHairTint;
            }

            float3 sharcRadiance;
            if (!arrivedViaDelta && isValidHit && SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
            {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
                if (giSecStarted)
                {
                    float3 relTp = throughput / max(giSecThroughput, 1e-10);
                    giSecRadiance += sharcRadiance * relTp;
                }
                else if (!fillState.hasFlag(kStablePlaneFlag_OnBranch))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(sharcRadiance * throughput) : 0;
                    fillPathL += float4(sharcRadiance * throughput, specAvg);
                }
#else
                sampleRadiance += sharcRadiance * throughput;
#endif
                break;
            }
#   endif // !SHARC_UPDATE
#endif // SHARC  
            
            brdfContext = BRDFContext::make(surface, -direction);
            isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
            if (!isEnter) {
                surface.FlipNormal();
                brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
            }

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
                    directRadiance += EvaluateSubsurfaceDiffuseNEE(surface, brdfContext, material, instance, payload, rayCone, randomSeed, surface.Primary);
                    isSssPath = true;
                    // Specular uses the standard path with diffuse suppressed
                    Surface specSurface = surface;
                    specSurface.DiffuseAlbedo = 0;
                    StandardBSDF specBsdf = StandardBSDF::make(specSurface, isEnter);
                    directRadiance += EvaluateDirectRadiance(material.Type, material.Feature, specSurface, brdfContext, instance, specBsdf, randomSeed, surface.Primary);
                }
                else
#endif
                { 
                    directRadiance += EvaluateDirectRadiance(material.Type, material.Feature, surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
                }
            }
            
            // Delta lobe lighting: check if delta reflection/refraction directions see any analytical lights
            if (bounceHasDeltaLobes)
            {
                directRadiance += EvalDeltaLobeLighting(surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
            }
            
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
            if (giSecStarted)
            {
                // ReSTIR GI owns radiance from secondary surface onward — divert everything
                float3 relTp = throughput / max(giSecThroughput, 1e-10);
                giSecRadiance += (directRadiance + surface.Emissive) * relTp;
            }
            else
            {
                // NEE/direct radiance: always accumulated (not captured in BUILD)
                if (any(directRadiance > 0))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(directRadiance * throughput) : 0;
                    fillPathL += float4(directRadiance * throughput, specAvg);
                }
                // Emissive: gated by OnBranch (BUILD already captured emissive along delta paths)
                if (!fillState.hasFlag(kStablePlaneFlag_OnBranch) && any(surface.Emissive > 0))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(surface.Emissive * throughput) : 0;
                    fillPathL += float4(surface.Emissive * throughput, specAvg);
                }
            }
#elif defined(SHARC) && SHARC_UPDATE
            sampleRadiance += directRadiance * throughput;
            if (!SharcUpdateHit(sharcParameters, sharcState, sharcHitData, directRadiance, Random(randomSeed)))
                return;

            throughput = float3(1.0f, 1.0f, 1.0f);
#else
            sampleRadiance += directRadiance * throughput;
            sampleRadiance += surface.Emissive * throughput;
#endif

        }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        // Commit remaining radiance to the current plane at path end
        spCtx.CommitDenoiserRadiance(idx, fillState.planeIndex, fillPathL);

#   if defined(RESTIR_GI)       
        // Write accumulated secondary radiance for ReSTIR GI
        if (giSecStarted)
            SecondaryGBufRadiance[idx] = float4(giSecRadiance, giSecPdf);
        else
            SecondaryGBufRadiance[idx] = float4(0, 0, 0, 0);
#   endif
        
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE        
#   if defined(NRD)
        float normHitDist = accumulatedHitDist;
        normHitDist = REBLUR_FrontEnd_GetNormHitDist(accumulatedHitDist, depthVS, Raytracing.HitDistSettings.xyz, isSpecularSample ? sourceSurface.Roughness : 1.0);
        
        if (isSpecularSample) {
            NRD_FrontEnd_SpecHitDistAveraging_Add(specHitDist, normHitDist);        
        } else {
            diffHitDist += normHitDist;
        }
#   endif
#endif        
        
        radiance += sampleRadiance;

#if defined(SHARC) && SHARC_UPDATE
        return;
#endif
    }

    radiance /= MAX_SAMPLES;        

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
    // FILL mode output: combine stable radiance (noise-free) with all planes' noisy radiance
    {
        float3 totalRadiance = spCtx.GetAllRadiance(idx, true);
        Output[idx] = float4(LLTrueLinearToGamma(totalRadiance), 1.0f);
#   if defined(DLSS_RR)    
        SpecularHitDistance[idx] = specHitDist;
#   endif
    }
#elif !(defined(SHARC) && SHARC_UPDATE)
    // REFERENCE mode output
    // Apply primary ray water absorption when camera is underwater
    if (Camera.IsUnderwater != 0 && any(Camera.UnderwaterAbsorption > 0.0f))
    {
        float3 primaryWaterAttenuation = exp(-Camera.UnderwaterAbsorption * sourcePayload.hitDistance);
        direct *= primaryWaterAttenuation;
        radiance *= primaryWaterAttenuation;
    }
    
#if defined(NRD)
    float3 diffuseRadiance = isSpecularSample ? 0.0.xxx : radiance;
    float3 specularRadiance = isSpecularSample ? radiance : 0.0.xxx;
    
    float3 diffFactor, specFactor;
    NRD_MaterialFactors(sourceSurface.Normal, sourceBRDFContext.ViewDirection, sourceSurface.DiffuseAlbedo, sourceSurface.F0, sourceSurface.Roughness, diffFactor, specFactor);    

    diffuseRadiance /= diffFactor;
    specularRadiance /= specFactor;    
    
    Output[idx] = float4(direct, 1.0f);
    DiffuseRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diffuseRadiance, diffHitDist, true);
    SpecularRadiance[idx] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(specularRadiance, specHitDist, true);  
    
    DiffuseFactor[idx] = diffFactor;
    SpecularFactor[idx] = specFactor;    
#else    
    Output[idx] = float4(LLTrueLinearToGamma(direct + radiance), 1.0f);

#   if defined(DLSS_RR)
    SpecularHitDistance[idx] = specHitDist;     
#   endif    
#endif
        
#endif    
}
