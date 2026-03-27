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

#include "raytracing/include/PathTracerStablePlanes.hlsli"

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
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularHitDistance[idx] = RAY_TMAX;
        MotionVectors[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        Depth[idx] = 1;  // sky → far plane (standard Z: 0=near, 1=far)
        return;
#else
        // REFERENCE: original behavior
#if !(defined(SHARC) && SHARC_UPDATE)
        Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);   
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);    
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularHitDistance[idx] = RAY_TMAX;
        
        float3 skyVirtualPos = Camera.Position.xyz + sourceDirection * kEnvironmentMapSceneDistance;
        MotionVectors[idx] = float4(computeMotionVector(skyVirtualPos, skyVirtualPos), 0);
        Depth[idx] = 1;  // sky → far plane (standard Z: 0=near, 1=far)        
#endif
        return;
#endif
    }
          
    RayCone sourceRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * sourcePayload.hitDistance, Raytracing.PixelConeSpreadAngle);   
    
    float3 sourcePosition = Camera.Position.xyz + sourceDirection * sourcePayload.hitDistance;
    
    Instance sourceInstance;
    Material sourceMaterial;

    Surface sourceSurface = SurfaceMaker::make(sourcePosition, sourcePayload, sourceDirection, sourceRayCone, sourceInstance, sourceMaterial, true);

    // Pass through Effect materials on primary ray: accumulate emissive, don't interact
    float3 primaryEffectEmissive = float3(0, 0, 0);
    [loop]
    for (uint effectPrimPass = 0; effectPrimPass < 16 && sourceMaterial.ShaderType == ShaderType::Effect; effectPrimPass++)
    {
        primaryEffectEmissive += sourceSurface.Emissive;

        float3 fn = dot(sourceDirection, sourceSurface.FaceNormal) <= 0.0f ? sourceSurface.FaceNormal : -sourceSurface.FaceNormal;
        sourceRay.Origin = OffsetRay(sourceSurface.Position, fn, true);
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
        DiffuseAlbedo[idx] = float3(0.0f, 0.0f, 0.0f);
        SpecularAlbedo[idx] = float3(0.5f, 0.5f, 0.5f);
        NormalRoughness[idx] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        SpecularHitDistance[idx] = RAY_TMAX;
        
        float3 skyVirtualPos = Camera.Position.xyz + sourceDirection * kEnvironmentMapSceneDistance;
        MotionVectors[idx] = float4(computeMotionVector(skyVirtualPos, skyVirtualPos), 0);
        Depth[idx] = 1;        
    #endif
        return;
#endif
    }

    float primarySceneDistance = length(sourcePosition - Camera.Position.xyz);

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
    
    // Write MV and Depth for REFERENCE mode (BUILD mode writes these in PathTracerStablePlanes)
#   if PATH_TRACER_MODE == PATH_TRACER_MODE_REFERENCE
    float3 hitPosW = sourcePosition;
    MotionVectors[idx] = float4(computeMotionVector(hitPosW, hitPosW), 0);
    Depth[idx] = computeClipDepth(hitPosW);
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
        // Compute MV from the primary surface position. All stable planes for this pixel
        // share this MV — it tracks the screen-space movement of the actual geometry, not
        // the virtual position deep in a delta reflection/refraction chain.
        float3 primaryHitPos = Camera.Position.xyz + sourceDirection * primarySceneDistance;
        float3 buildMVs = computeMotionVector(primaryHitPos, primaryHitPos);
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
            Material buildMaterial;
            RayCone buildRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * buildSceneLength, Raytracing.PixelConeSpreadAngle);
            Surface buildSurface = SurfaceMaker::make(buildHitPos, buildPayload, hitResult.nextRayDir, buildRayCone, buildInstance, buildMaterial, true);
            BRDFContext buildBrdfCtx = BRDFContext::make(buildSurface, -hitResult.nextRayDir);
            bool buildIsEnter = dot(buildSurface.FaceNormal, buildBrdfCtx.ViewDirection) >= 0.0f;
            if (!buildIsEnter) buildSurface.FlipNormal();
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
                Material expMaterial;
                RayCone expRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * expSceneLength, Raytracing.PixelConeSpreadAngle);
                Surface expSurface = SurfaceMaker::make(expHitPos, expPayload, ep.rayDir, expRayCone, expInstance, expMaterial, false);
                BRDFContext expBrdfCtx = BRDFContext::make(expSurface, -ep.rayDir);
                bool expIsEnter = dot(expSurface.FaceNormal, expBrdfCtx.ViewDirection) >= 0.0f;
                if (!expIsEnter) expSurface.FlipNormal();
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
                    Material contMaterial;
                    RayCone contRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * expSceneLength, Raytracing.PixelConeSpreadAngle);
                    Surface contSurface = SurfaceMaker::make(contHitPos, contPayload, expHitResult.nextRayDir, contRayCone, contInstance, contMaterial, false);
                    BRDFContext contBrdfCtx = BRDFContext::make(contSurface, -expHitResult.nextRayDir);
                    bool contIsEnter = dot(contSurface.FaceNormal, contBrdfCtx.ViewDirection) >= 0.0f;
                    if (!contIsEnter) contSurface.FlipNormal();
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
    {
        float3 fillRayOrigin, fillRayDir, fillThp;
        float fillSceneLength;
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
        if (!sourceIsEnter) sourceSurface.FlipNormal();
        AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);
        sourceBSDF = StandardBSDF::make(sourceSurface, sourceIsEnter);
        fillPlaneThp = fillThp;
    }

    // Update GBuffer with the stable plane's base surface data (used by DLSS-RR).
    // Diffuse/normal follow the dominant plane, but specular albedo stays on the first plane.
    uint dominantPlane = spCtx.LoadDominantIndex(idx);
    if (dominantPlane != 0)
    {
        StablePlane domSP = spCtx.LoadStablePlane(idx, dominantPlane);
        float domRoughness = domSP.GetRoughness();
        float3 domNormal = domSP.GetNormal();
        float3 domDiffEst, domSpecEst;
        UnpackTwoFp32ToFp16(domSP.DenoiserPackedBSDFEstimate, domDiffEst, domSpecEst);
        DiffuseAlbedo[idx] = domDiffEst;
        NormalRoughness[idx] = float4(domNormal, domRoughness);
    }
    else
    {
        DiffuseAlbedo[idx] = sourceSurface.DiffuseAlbedo;
        NormalRoughness[idx] = float4(sourceSurface.Normal, sourceSurface.Roughness);
    }

    {
        const float2 envBRDF2 = BRDF::EnvBRDF(sourceSurface.Roughness, sourceBRDFContext.NdotV);
        SpecularAlbedo[idx] = float3(sourceSurface.F0 * envBRDF2.x + envBRDF2.y);
    }

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
                direct += EvaluateDirectRadiance(sourceMaterial, specSurface, sourceBRDFContext, sourceInstance, specBsdf, randomSeed, true);
            }
            else
#endif
                direct += EvaluateDirectRadiance(sourceMaterial, sourceSurface, sourceBRDFContext, sourceInstance, sourceBSDF, randomSeed, true);
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
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
        float3 throughput = fillPlaneThp; // start with plane's delta path throughput
#else
        float3 throughput = float3(1.0f, 1.0f, 1.0f);
#endif
        float materialRoughnessPrev = 0.0f;
        bool isEnter = sourceIsEnter;

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
            
#if LIGHTING_MODE == LIGHTING_MODE_DIFFUSE
            direction = surface.Mul(SampleCosineHemisphere(randomSeed));

            float NdotD = saturate(dot(surface.Normal, direction));

            throughput *= surface.AO;
            throughput *= surface.Albedo;
            
            const bool hasTransmission = false;
#else            
            bool isValid = bsdf.SampleBSDF(brdfContext, material, surface, bsdfSample, randomSeed);
            
            if (isValid)
                direction = bsdfSample.wo;
            else
                break;
            
            bool isDelta = bsdfSample.isLobe(LobeType::Delta);
            isSpecular = bsdfSample.isLobe(LobeType::Specular) || isDelta;
            bool hasTransmission = bsdfSample.isLobe(LobeType::Transmission);
            isPrimaryReplacement = surface.Primary && isDelta;

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
            
            if (isSpecular)
                specHitDist += payload.hitDistance;

            if (!payload.Hit())
            {
                float3 skyIrradiance = SampleSky(SkyHemisphere, direction) * Raytracing.Sky;

#if defined(SHARC) && SHARC_UPDATE
                SharcUpdateMiss(sharcParameters, sharcState, skyIrradiance);
#elif PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
                // In FILL mode: skip sky if on stable branch (already captured in BUILD)
                if (!fillState.hasFlag(kStablePlaneFlag_OnBranch))
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

            // Pass through Effect materials in bounce: accumulate emissive, continue ray unchanged
            bool effectMiss = false;
            [loop]
            for (uint effectBouncePass = 0; effectBouncePass < 16 && material.ShaderType == ShaderType::Effect; effectBouncePass++)
            {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
                if (!fillState.hasFlag(kStablePlaneFlag_OnBranch) && any(surface.Emissive > 0))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(surface.Emissive * throughput) : 0;
                    fillPathL += float4(surface.Emissive * throughput, specAvg);
                }
#else
                sampleRadiance += surface.Emissive * throughput;
#endif
                float3 fn = dot(direction, surface.FaceNormal) <= 0.0f ? surface.FaceNormal : -surface.FaceNormal;
                ray.Origin = OffsetRay(surface.Position, fn, true);
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
                if (!fillState.hasFlag(kStablePlaneFlag_OnBranch))
                {
                    float specAvg = isSpecular ? Color::RGBToLuminance(skyIrradiance * throughput) : 0;
                    fillPathL += float4(skyIrradiance * throughput, specAvg);
                }
#else
                sampleRadiance += skyIrradiance * throughput;
#endif
                break;
            }

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
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
                if (!fillState.hasFlag(kStablePlaneFlag_OnBranch))
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
                    directRadiance += EvaluateSubsurfaceDiffuseNEE(surface, brdfContext, material, instance, payload, rayCone, randomSeed, surface.Primary);
                    isSssPath = true;
                    // Specular uses the standard path with diffuse suppressed
                    Surface specSurface = surface;
                    specSurface.DiffuseAlbedo = 0;
                    StandardBSDF specBsdf = StandardBSDF::make(specSurface, isEnter);
                    directRadiance += EvaluateDirectRadiance(material, specSurface, brdfContext, instance, specBsdf, randomSeed, surface.Primary);
                }
                else
#endif
                { 
                    directRadiance += EvaluateDirectRadiance(material, surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
                }
            }
            
            // Delta lobe lighting: check if delta reflection/refraction directions see any analytical lights
            if (bounceHasDeltaLobes)
            {
                directRadiance += EvalDeltaLobeLighting(surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
            }
            
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
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
        SpecularHitDistance[idx] = specHitDist;
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
    Output[idx] = float4(LLTrueLinearToGamma(direct + radiance), 1.0f);
    SpecularHitDistance[idx] = specHitDist;     
#endif    
}
