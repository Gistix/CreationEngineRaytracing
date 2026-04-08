/*
 * PathTracerStablePlanes.hlsli
 *
 * BUILD/FILL pass logic for the Stable Planes system.
 */

#ifndef __PATH_TRACER_STABLE_PLANES_HLSLI__
#define __PATH_TRACER_STABLE_PLANES_HLSLI__

#include "raytracing/include/StablePlanes.hlsli"
#include "raytracing/include/Materials/BSDF.hlsli"

// ============================================================================
// PSR Motion Vector and Depth
// ============================================================================

void computePSRMotionVectorsAndDepth(
    const uint2 pixelPos,
    const float totalSceneLength,
    const float3x3 imageXform,
    const float3 surfacePosition,
    const float3 surfacePrevPosition,
    out float3 outMotionVectors,
    out float outDepth)
{
    float3 cameraRayDir = normalize(mul((float3x3)Camera.ViewInverse,
        GetView(pixelPos, Camera.RenderSize, Camera.ProjInverse)));
    float3 virtualWorldPos = Camera.Position.xyz + cameraRayDir * totalSceneLength;
    float3 worldMotion = surfacePrevPosition - surfacePosition;
    float3 virtualMotion = mul(imageXform, worldMotion);
    outMotionVectors = computeMotionVector(virtualWorldPos, virtualWorldPos + virtualMotion);
    outDepth = computeClipDepth(virtualWorldPos);
}

#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES

// ============================================================================
// BUILD Pass Exploration Payload
// ============================================================================

struct StablePlaneExplorationPayload
{
    float3  rayOrigin;
    float3  rayDir;
    float3  throughput;
    float3  motionVectors;
    float   sceneLength;        // accumulated ray travel before this segment
    uint    stableBranchID;
    uint    vertexIndex;
    float3x3 imageXform;
    float   roughnessAccum;
    bool    insideWaterVolume;
    float3  waterVolumeAbsorption;

    void Pack(out uint4 packed[5])
    {
        packed[0] = uint4(asuint(rayOrigin), asuint(sceneLength));
        packed[1] = uint4(asuint(rayDir), stableBranchID);
        packed[2] = uint4(PackTwoFp32ToFp16(throughput, motionVectors), vertexIndex);
        packed[3] = uint4(
            PackTwoFp32ToFp16(imageXform[0], imageXform[1]),
            asuint(roughnessAccum)
        );
        packed[4] = uint4(
            Fp32ToFp16(float4(imageXform[2], 0)),
            (insideWaterVolume ? 1u : 0u) | (f32tof16(clamp(waterVolumeAbsorption.x, 0, HLF_MAX)) << 16),
            f32tof16(clamp(waterVolumeAbsorption.y, 0, HLF_MAX)) | (f32tof16(clamp(waterVolumeAbsorption.z, 0, HLF_MAX)) << 16)
        );
    }

    static StablePlaneExplorationPayload Unpack(uint4 packed[5])
    {
        StablePlaneExplorationPayload p;
        p.rayOrigin         = asfloat(packed[0].xyz);
        p.sceneLength       = asfloat(packed[0].w);
        p.rayDir            = asfloat(packed[1].xyz);
        p.stableBranchID    = packed[1].w;
        float3 thp, mvs;
        UnpackTwoFp32ToFp16(packed[2].xyz, thp, mvs);
        p.throughput        = thp;
        p.motionVectors     = mvs;
        p.vertexIndex       = packed[2].w;
        float3 row0, row1;
        UnpackTwoFp32ToFp16(packed[3].xyz, row0, row1);
        p.imageXform[0]     = row0;
        p.imageXform[1]     = row1;
        p.roughnessAccum    = asfloat(packed[3].w);
        float4 row2ext      = Fp16ToFp32(packed[4].xy);
        p.imageXform[2]     = row2ext.xyz;
        p.insideWaterVolume     = (packed[4].z & 1u) != 0;
        p.waterVolumeAbsorption = float3(
            f16tof32(packed[4].z >> 16),
            f16tof32(packed[4].w & 0xFFFF),
            f16tof32(packed[4].w >> 16));
        return p;
    }
};

// ============================================================================
// imageXform Helpers
// ============================================================================

float3x3 MirrorReflectionMatrix(float3 n)
{
    return float3x3(
        1.0 - 2.0*n.x*n.x,  -2.0*n.x*n.y,       -2.0*n.x*n.z,
        -2.0*n.y*n.x,         1.0 - 2.0*n.y*n.y,  -2.0*n.y*n.z,
        -2.0*n.z*n.x,        -2.0*n.z*n.y,         1.0 - 2.0*n.z*n.z
    );
}

float3x3 UpdateImageXform(float3x3 prevXform, float3 inDir, float3 outDir, float3 normal, bool isTransmission)
{
    float3x3 bounceMatrix;
    if (isTransmission)
    {
        bounceMatrix = MatrixRotateFromTo(-inDir, outDir);
    }
    else
    {
        bounceMatrix = MirrorReflectionMatrix(normal);
    }
    return mul(bounceMatrix, prevXform);
}

// ============================================================================
// SplitDeltaPath
// ============================================================================

StablePlaneExplorationPayload SplitDeltaPath(
    const float3 surfacePosition,
    const float3 faceNormal,
    const DeltaLobe lobe,
    const uint deltaLobeIndex,
    const uint prevBranchID,
    const uint prevVertexIndex,
    const float3 prevThp,
    const float3 prevMotionVectors,
    const float prevSceneLength,
    const float3x3 prevImageXform,
    const float prevRoughnessAccum,
    const float3 incomingDir,    // direction towards surface (not view dir)
    const bool prevInsideWater,
    const float3 prevWaterAbsorption,
    const bool isEnterSurface,
    const float3 surfaceVolumeAbsorption,
    const float positionError
#if USE_SIA_INTERPOLATION
    , const float siaOffset
#endif
    )
{
    StablePlaneExplorationPayload payload;

    payload.stableBranchID = StablePlanesAdvanceBranchID(prevBranchID, deltaLobeIndex);
    payload.vertexIndex    = prevVertexIndex + 1;
    payload.rayDir         = lobe.dir;
#if USE_SIA_INTERPOLATION
    payload.rayOrigin      = OffsetRaySIA(surfacePosition, faceNormal, siaOffset, lobe.transmission != 0);
#else
    payload.rayOrigin      = OffsetRay(surfacePosition, faceNormal, positionError, lobe.transmission != 0);
#endif
    payload.throughput     = prevThp * lobe.thp;
    payload.motionVectors  = prevMotionVectors;
    payload.sceneLength    = prevSceneLength;
    payload.roughnessAccum = prevRoughnessAccum;

    payload.imageXform = UpdateImageXform(prevImageXform, incomingDir, lobe.dir, faceNormal, lobe.transmission != 0);

    if (lobe.transmission != 0 && any(surfaceVolumeAbsorption > 0.0f))
    {
        payload.insideWaterVolume     = isEnterSurface;
        payload.waterVolumeAbsorption = isEnterSurface ? surfaceVolumeAbsorption : float3(0, 0, 0);
    }
    else
    {
        payload.insideWaterVolume     = prevInsideWater;
        payload.waterVolumeAbsorption = prevWaterAbsorption;
    }

    return payload;
}

// ============================================================================
// StablePlanesHandleMiss
// ============================================================================

void StablePlanesHandleMiss(
    inout StablePlanesContext ctx,
    const uint2 pixelPos,
    const uint planeIndex,
    const uint vertexIndex,
    const uint stableBranchID,
    const float3 rayOrigin,
    const float3 rayDir,
    const float3 throughput,
    const float3 motionVectors,
    const float3x3 imageXform,
    const float3 skyRadiance,
    const bool isDominant)
{
    float3 skyMV; float skyDepth;
    computePSRMotionVectorsAndDepth(pixelPos, kEnvironmentMapSceneDistance, imageXform,
        float3(0,0,0), float3(0,0,0), skyMV, skyDepth);

    float3 planeNormal = -rayDir;

    ctx.StoreStablePlane(
        pixelPos, planeIndex, vertexIndex,
        rayOrigin, rayDir, stableBranchID,
        1.0 / 0.0,
        kEnvironmentMapSceneDistance,
        throughput, skyMV,
        1.0,
        planeNormal,
        float3(1,1,1),
        float3(0,0,0),
        isDominant,
        /* flagsAndVertexIndex */ 0,
        0
    );

    if (isDominant)
    {
        MotionVectors[pixelPos] = float4(skyMV, 0);
        Depth[pixelPos] = skyDepth;
    }

    ctx.AccumulateStableRadiance(pixelPos, skyRadiance * throughput);
}

// ============================================================================
// StablePlanesHandleHit
// ============================================================================

struct StablePlanesHitResult
{
    bool        continueTracing;    // true = delta-only, keep tracing this lobe
    float3      nextRayOrigin;
    float3      nextRayDir;
    float3      nextThp;
    uint        nextBranchID;
    uint        nextVertexIndex;
    float3x3    nextImageXform;
    float       nextRoughnessAccum;
    bool        nextInsideWater;     // water volume state for next segment
    float3      nextWaterAbsorption; // Beer-Lambert absorption for next segment
};

StablePlanesHitResult StablePlanesHandleHit(
    inout StablePlanesContext ctx,
    const uint2 pixelPos,
    const uint planeIndex,
    const uint vertexIndex,
    const uint stableBranchID,
    const float3 rayOrigin,
    const float3 rayDir,
    const float hitDistance,
    const float totalSceneLength,
    const float3 throughput,
    const float3 motionVectors,
    const float3x3 imageXform,
    const float roughnessAccum,
    const Surface surface,
    const BRDFContext brdfContext,
    const StandardBSDF bsdf,
    const bool isDominant,
    const Instance instance,
    inout uint randomSeed,
    const bool insideWaterVolume,
    const float3 waterVolumeAbsorption)
{
    StablePlanesHitResult result;
    result.continueTracing   = false;
    result.nextRayOrigin     = 0;
    result.nextRayDir        = 0;
    result.nextThp           = 0;
    result.nextBranchID      = cStablePlaneInvalidBranchID;
    result.nextVertexIndex   = vertexIndex;
    result.nextImageXform    = imageXform;
    result.nextRoughnessAccum = roughnessAccum;
    result.nextInsideWater    = insideWaterVolume;
    result.nextWaterAbsorption = waterVolumeAbsorption;

    uint waterFlags = (insideWaterVolume ? 1u : 0u) | (f32tof16(clamp(waterVolumeAbsorption.x, 0, HLF_MAX)) << 16);
    uint waterCounters = f32tof16(clamp(waterVolumeAbsorption.y, 0, HLF_MAX)) | (f32tof16(clamp(waterVolumeAbsorption.z, 0, HLF_MAX)) << 16);
    bool isEnterSurface = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0;

    // Coat-priority GBuffer: use coat layer properties for denoiser when coat is present
    bool hasCoat = surface.CoatStrength > 0;
    float  gbufferRoughness = hasCoat ? surface.CoatRoughness : surface.Roughness;
    float3 gbufferNormal    = hasCoat ? surface.CoatNormal    : surface.Normal;
    float3 coatTint         = lerp(float3(1,1,1), surface.CoatColor, surface.CoatStrength);

    float3 emissive = surface.Emissive;
    if (any(emissive > 0))
        ctx.AccumulateStableRadiance(pixelPos, emissive * throughput);

    if (vertexIndex >= ctx.maxStablePlaneVertexDepth)
    {
        float3 faceNormal = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0 ? surface.FaceNormal : -surface.FaceNormal;
        float3 psrMV; float psrDepth;
        computePSRMotionVectorsAndDepth(pixelPos, totalSceneLength, imageXform,
            surface.Position, surface.PrevPosition, psrMV, psrDepth);
        ctx.StoreStablePlane(
            pixelPos, planeIndex, vertexIndex,
            rayOrigin, rayDir, stableBranchID,
            totalSceneLength, hitDistance,
            throughput, psrMV,
            gbufferRoughness, gbufferNormal,
            surface.DiffuseAlbedo.xxx * coatTint, hasCoat ? surface.CoatF0 : surface.F0,
            isDominant, waterFlags, waterCounters
        );
        if (isDominant)
        {
            MotionVectors[pixelPos] = float4(psrMV, 0);
            Depth[pixelPos] = psrDepth;
        }
        return result;
    }

    DeltaLobe deltaLobes[cMaxDeltaLobes];
    int deltaLobeCount;
    float nonDeltaPart;
    bsdf.EvalDeltaLobes(brdfContext, surface, deltaLobes, deltaLobeCount, nonDeltaPart);

    for (int k = 0; k < deltaLobeCount; k++)
    {
        if (Average3(abs(deltaLobes[k].thp)) < 0.001)
            deltaLobes[k].thp = 0;
    }

    int activeDeltaLobes = 0;
    int firstActiveLobe = -1;
    for (int k2 = 0; k2 < deltaLobeCount; k2++)
    {
        if (any(deltaLobes[k2].thp > 0))
        {
            if (firstActiveLobe < 0) firstActiveLobe = k2;
            activeDeltaLobes++;
        }
    }

    float3 faceNormal = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0 ? surface.FaceNormal : -surface.FaceNormal;

    if (nonDeltaPart > 1e-5 || activeDeltaLobes == 0)
    {
        float3 diffBSDFEstimate = max(surface.DiffuseAlbedo, 0.04) * coatTint;
        float3 specBSDFEstimate = max(hasCoat ? surface.CoatF0 : surface.F0, 0.04);

        float3 psrMV; float psrDepth;
        computePSRMotionVectorsAndDepth(pixelPos, totalSceneLength, imageXform,
            surface.Position, surface.PrevPosition, psrMV, psrDepth);
        ctx.StoreStablePlane(
            pixelPos, planeIndex, vertexIndex,
            rayOrigin, rayDir, stableBranchID,
            totalSceneLength, hitDistance,
            throughput, psrMV,
            gbufferRoughness, gbufferNormal,
            diffBSDFEstimate, specBSDFEstimate,
            isDominant, waterFlags, waterCounters
        );
        if (isDominant)
        {
            MotionVectors[pixelPos] = float4(psrMV, 0);
            Depth[pixelPos] = psrDepth;
        }
        return result;
    }

    // Evaluate delta lobe lighting (deterministic, captured in stable radiance)
    {
        float3 deltaLighting = EvalDeltaLobeLighting(surface, brdfContext, instance, bsdf, randomSeed, true);
        if (any(deltaLighting > 0))
            ctx.AccumulateStableRadiance(pixelPos, deltaLighting * throughput);
    }

    bool canReuseCurrent = (planeIndex != 0) || (activeDeltaLobes == 1);

    int availableCount = 0;
    int availablePlanes[cStablePlaneCount];
    ctx.GetAvailableEmptyPlanes(pixelPos, availableCount, availablePlanes);

    int forkedCount = 0;

    for (int lobeIdx = 0; lobeIdx < deltaLobeCount; lobeIdx++)
    {
        if (!any(deltaLobes[lobeIdx].thp > 0))
            continue;

        if (canReuseCurrent && lobeIdx == firstActiveLobe)
        {
            result.continueTracing      = true;
            result.nextRayDir           = deltaLobes[lobeIdx].dir;
#if USE_SIA_INTERPOLATION
            result.nextRayOrigin        = OffsetRaySIA(surface.Position, faceNormal, surface.SIAOffset, deltaLobes[lobeIdx].transmission != 0);
#else
            result.nextRayOrigin        = OffsetRay(surface.Position, faceNormal, surface.PositionError, deltaLobes[lobeIdx].transmission != 0);
#endif
            result.nextThp              = throughput * deltaLobes[lobeIdx].thp;
            result.nextBranchID         = StablePlanesAdvanceBranchID(stableBranchID, lobeIdx);
            result.nextVertexIndex      = vertexIndex + 1;
            result.nextImageXform       = UpdateImageXform(imageXform, -rayDir, deltaLobes[lobeIdx].dir, faceNormal, deltaLobes[lobeIdx].transmission != 0);
            result.nextRoughnessAccum   = roughnessAccum;

            if (deltaLobes[lobeIdx].transmission != 0 && any(surface.VolumeAbsorption > 0.0f))
            {
                result.nextInsideWater     = isEnterSurface;
                result.nextWaterAbsorption = isEnterSurface ? surface.VolumeAbsorption : float3(0, 0, 0);
            }
        }
        else
        {
            if (forkedCount < availableCount)
            {
                int targetPlane = availablePlanes[forkedCount];
                forkedCount++;

                StablePlaneExplorationPayload forkPayload = SplitDeltaPath(
                    surface.Position, faceNormal, deltaLobes[lobeIdx], lobeIdx,
                    stableBranchID, vertexIndex, throughput, motionVectors,
                    totalSceneLength, imageXform, roughnessAccum, -rayDir,
                    insideWaterVolume, waterVolumeAbsorption, isEnterSurface, surface.VolumeAbsorption,
                    surface.PositionError
#if USE_SIA_INTERPOLATION
                    , surface.SIAOffset
#endif
                );

                uint4 packed[5];
                forkPayload.Pack(packed);
                ctx.StoreExplorationStart(pixelPos, targetPlane, packed);
            }
        }
    }

    if (!result.continueTracing)
    {
        bool storeAsDominant = isDominant && canReuseCurrent;
        float3 psrMV; float psrDepth;
        computePSRMotionVectorsAndDepth(pixelPos, totalSceneLength, imageXform,
            surface.Position, surface.PrevPosition, psrMV, psrDepth);
        ctx.StoreStablePlane(
            pixelPos, planeIndex, vertexIndex,
            rayOrigin, rayDir, stableBranchID,
            totalSceneLength, hitDistance,
            throughput, psrMV,
            gbufferRoughness, gbufferNormal,
            max(surface.DiffuseAlbedo, 0.04) * coatTint, max(hasCoat ? surface.CoatF0 : surface.F0, 0.04),
            storeAsDominant, waterFlags, waterCounters
        );
        if (storeAsDominant)
        {
            MotionVectors[pixelPos] = float4(psrMV, 0);
            Depth[pixelPos] = psrDepth;
        }
    }

    return result;
}

#endif // PATH_TRACER_MODE_BUILD_STABLE_PLANES

// ============================================================================
// FILL Pass Helpers
// ============================================================================

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES

static const uint kStablePlaneFlag_OnPlane          = 1 << 0;
static const uint kStablePlaneFlag_OnBranch         = 1 << 1;
static const uint kStablePlaneFlag_OnDominant       = 1 << 2;
static const uint kStablePlaneFlag_BaseScatterDiff  = 1 << 3;

struct StablePlaneFillState
{
    uint    stableBranchID;
    uint    planeIndex;
    uint    flags;
    uint    bouncesFromPlane;

    bool hasFlag(uint f)    { return (flags & f) != 0; }
    void setFlag(uint f, bool v) { if(v) flags |= f; else flags &= ~f; }
};

float2 FirstHitFromVBuffer(
    inout StablePlaneFillState fillState,
    inout float3 rayOrigin,
    inout float3 rayDir,
    inout float3 throughput,
    out float sceneLength,
    out uint vertexIndex,
    out bool outInsideWaterVolume,
    out float3 outWaterVolumeAbsorption,
    const StablePlanesContext ctx,
    const uint2 pixelPos,
    const uint basePlaneIndex)
{
    float2 tMinMax = float2(0, kMaxRayTravel);

    StablePlane sp = ctx.LoadStablePlane(pixelPos, basePlaneIndex);
    uint branchID = ctx.GetBranchID(pixelPos, basePlaneIndex);

    sceneLength = sp.SceneLength;
    float lastRayTCurrent = sp.LastRayTCurrent;
    vertexIndex = sp.GetVertexIndex();

    float3 thp, dummy;
    UnpackTwoFp32ToFp16(sp.PackedThpAndMVs, thp, dummy);

    outInsideWaterVolume = sp.GetInsideWaterVolume();
    outWaterVolumeAbsorption = sp.GetWaterVolumeAbsorption();

    bool isMiss = !isfinite(sceneLength);

    if (!isMiss)
    {
        tMinMax.x = lastRayTCurrent * 0.99;
        tMinMax.y = lastRayTCurrent * 1.01;
    }

    rayOrigin   = sp.RayOrigin;
    rayDir      = sp.RayDir;
    throughput  = thp;

    fillState.stableBranchID    = branchID;
    fillState.planeIndex        = basePlaneIndex;
    fillState.flags             = kStablePlaneFlag_OnPlane | kStablePlaneFlag_OnBranch;
    fillState.bouncesFromPlane  = 0;

    uint dominantIdx = ctx.LoadDominantIndex(pixelPos);
    fillState.setFlag(kStablePlaneFlag_OnDominant, dominantIdx == basePlaneIndex);

    return isMiss ? float2(-1, -1) : tMinMax;
}

void StablePlanesOnScatter(
    inout StablePlaneFillState fillState,
    inout float4 pathL,
    const BSDFSample bsdfSample,
    const uint nextVertexIndex,
    const StablePlanesContext ctx,
    const uint2 pixelPos)
{
    bool wasOnPlane = fillState.hasFlag(kStablePlaneFlag_OnPlane);
    if (wasOnPlane)
    {
        fillState.setFlag(kStablePlaneFlag_BaseScatterDiff, bsdfSample.isLobe(LobeType::Diffuse));
    }
    fillState.setFlag(kStablePlaneFlag_OnPlane, false);

    if (fillState.hasFlag(kStablePlaneFlag_OnBranch) && nextVertexIndex <= cStablePlaneMaxVertexIndex)
    {
        fillState.stableBranchID = StablePlanesAdvanceBranchID(fillState.stableBranchID, bsdfSample.getDeltaLobeIndex());

        bool onStablePath = false;
        for (uint spi = 0; spi < cStablePlaneCount; spi++)
        {
            uint planeBranchID = ctx.GetBranchID(pixelPos, spi);
            if (planeBranchID == cStablePlaneInvalidBranchID)
                continue;

            if (StablePlaneIsOnPlane(planeBranchID, fillState.stableBranchID))
            {
                ctx.CommitDenoiserRadiance(pixelPos, fillState.planeIndex, pathL);

                fillState.planeIndex = spi;
                fillState.setFlag(kStablePlaneFlag_OnDominant, spi == ctx.LoadDominantIndex(pixelPos));
                fillState.setFlag(kStablePlaneFlag_OnPlane, true);
                fillState.bouncesFromPlane = 0;
                onStablePath = true;
                break;
            }

            uint planeVertexIndex = StablePlanesVertexIndexFromBranchID(planeBranchID);
            onStablePath |= StablePlaneIsOnStablePath(planeBranchID, planeVertexIndex, fillState.stableBranchID, nextVertexIndex);
        }
        fillState.setFlag(kStablePlaneFlag_OnBranch, onStablePath);
    }
    else
    {
        fillState.stableBranchID = cStablePlaneInvalidBranchID;
        fillState.setFlag(kStablePlaneFlag_OnBranch, false);
        fillState.bouncesFromPlane++;
    }

    if (!fillState.hasFlag(kStablePlaneFlag_OnPlane))
        fillState.bouncesFromPlane++;
}

#endif // FILL

#endif // __PATH_TRACER_STABLE_PLANES_HLSLI__
