/*
 * PathTracerStablePlanes.hlsli
 *
 * BUILD pass logic for the Stable Planes system.
 * Contains: SplitDeltaPath, StablePlanesHandleHit, StablePlanesHandleMiss
 *
 * Called from RayGeneration.hlsl when PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES.
 * The BUILD pass performs deterministic Whitted-style tracing through delta (mirror/glass) surfaces,
 * discovering the delta tree and assigning stable planes for each terminal non-delta base surface.
 *
 * Requires:
 *   - StablePlanes.hlsli (data structures, StablePlanesContext)
 *   - BSDF.hlsli (DeltaLobe, EvalDeltaLobes)
 *   - LobeType.hlsli
 *   - Surface.hlsli, BRDFContext
 */

#ifndef __PATH_TRACER_STABLE_PLANES_HLSLI__
#define __PATH_TRACER_STABLE_PLANES_HLSLI__

#include "raytracing/include/StablePlanes.hlsli"
#include "raytracing/include/Materials/BSDF.hlsli"

#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES

// ============================================================================
// BUILD Pass Exploration Payload
// ============================================================================
// The exploration payload stores the complete state needed to resume a forked
// delta path. It's serialized into a StablePlane's memory via PackCustomPayload.
// Layout: 5 × uint4 = 80 bytes (matches StablePlane size exactly)

struct StablePlaneExplorationPayload
{
    float3  rayOrigin;
    float3  rayDir;
    float3  throughput;
    float3  motionVectors;
    float   sceneLength;        // accumulated ray travel before this segment
    uint    stableBranchID;
    uint    vertexIndex;
    float3x3 imageXform;        // accumulated virtual-position transform
    float   roughnessAccum;     // accumulated roughness along the path

    void Pack(out uint4 packed[5])
    {
        packed[0] = uint4(asuint(rayOrigin), asuint(sceneLength));
        packed[1] = uint4(asuint(rayDir), stableBranchID);
        packed[2] = uint4(PackTwoFp32ToFp16(throughput, motionVectors), vertexIndex);
        // Store imageXform rows 0 and 1 as fp16 (6 floats → 3 uints)
        packed[3] = uint4(
            PackTwoFp32ToFp16(imageXform[0], imageXform[1]),
            asuint(roughnessAccum)
        );
        // Store imageXform row 2 as fp16 (3 floats → 2 uint halves)
        packed[4] = uint4(
            Fp32ToFp16(float4(imageXform[2], 0)),
            0, 0
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
        return p;
    }
};

// ============================================================================
// Motion Vector computation
// ============================================================================
// Computes 2.5D screen-space motion vector from a pair of world-space positions.
// Returns float3(pixelDisplacementXY, viewDepthDelta).
// Uses Camera.ViewProj (current, unjittered) and Camera.PrevViewProj (previous, unjittered).

float3 computeMotionVector(float3 posW, float3 prevPosW)
{
    float4 clipPos = mul(float4(posW, 1.0), Camera.ViewProj);
    clipPos.xyz /= clipPos.w;

    float4 prevClipPos = mul(float4(prevPosW, 1.0), Camera.PrevViewProj);
    prevClipPos.xyz /= prevClipPos.w;

    float3 motion;
    motion.xy = (prevClipPos.xy - clipPos.xy) * float2(0.5, -0.5) * Camera.RenderSize;
    motion.z = prevClipPos.w - clipPos.w;
    return motion;
}

// ============================================================================
// imageXform computation helpers
// ============================================================================

// Mirror reflection matrix for a given surface normal: I - 2*n*n^T
float3x3 MirrorReflectionMatrix(float3 n)
{
    return float3x3(
        1.0 - 2.0*n.x*n.x,  -2.0*n.x*n.y,       -2.0*n.x*n.z,
        -2.0*n.y*n.x,         1.0 - 2.0*n.y*n.y,  -2.0*n.y*n.z,
        -2.0*n.z*n.x,        -2.0*n.z*n.y,         1.0 - 2.0*n.z*n.z
    );
}

// Update imageXform for a delta bounce:
// - Reflection: multiply by mirror matrix
// - Refraction: multiply by rotation matrix from incident to refracted direction
float3x3 UpdateImageXform(float3x3 prevXform, float3 inDir, float3 outDir, float3 normal, bool isTransmission)
{
    float3x3 bounceMatrix;
    if (isTransmission)
    {
        // Refraction: rotation from inDir to outDir
        bounceMatrix = MatrixRotateFromTo(inDir, outDir);
    }
    else
    {
        // Reflection: mirror matrix
        bounceMatrix = MirrorReflectionMatrix(normal);
    }
    return mul(bounceMatrix, prevXform);
}

// ============================================================================
// SplitDeltaPath — Fork a new exploration path for a delta lobe
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
    const float3 incomingDir)    // direction towards surface (not view dir)
{
    StablePlaneExplorationPayload payload;

    payload.stableBranchID = StablePlanesAdvanceBranchID(prevBranchID, deltaLobeIndex);
    payload.vertexIndex    = prevVertexIndex + 1;
    payload.rayDir         = lobe.dir;
    payload.rayOrigin      = OffsetRay(surfacePosition, faceNormal, lobe.transmission != 0);
    payload.throughput     = prevThp * lobe.thp;
    payload.motionVectors  = prevMotionVectors;
    payload.sceneLength    = prevSceneLength;
    payload.roughnessAccum = prevRoughnessAccum;

    // Update imageXform for this bounce
    payload.imageXform = UpdateImageXform(prevImageXform, incomingDir, lobe.dir, faceNormal, lobe.transmission != 0);

    return payload;
}

// ============================================================================
// StablePlanesHandleMiss — Sky miss during BUILD pass
// ============================================================================
// When a delta path misses all geometry and hits the sky, we store a plane
// with SceneLength = inf, and accumulate sky radiance to StableRadiance.

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
    // Compute virtual motion vectors for sky hit:
    // Use a very distant virtual position along the ray
    float3 virtualWorldPos = rayOrigin + rayDir * kEnvironmentMapSceneDistance;
    // Sky has no object motion, MV comes purely from camera movement
    float3 skyMV = computeMotionVector(virtualWorldPos, virtualWorldPos);

    // For sky, normal doesn't matter much, use the ray direction
    float3 planeNormal = -rayDir;

    ctx.StoreStablePlane(
        pixelPos, planeIndex, vertexIndex,
        rayOrigin, rayDir, stableBranchID,
        /* sceneLength */ 1.0 / 0.0,  // +inf for sky miss
        /* rayTCurrent */ kEnvironmentMapSceneDistance,
        throughput, skyMV,
        /* roughness */ 1.0,          // sky = fully rough for denoiser
        planeNormal,
        /* diffBSDFEstimate */ float3(1,1,1),
        /* specBSDFEstimate */ float3(0,0,0),
        isDominant,
        /* flagsAndVertexIndex */ 0,
        /* packedCounters */ 0
    );

    // Write dominant plane MV to global output
    if (isDominant)
        MotionVectors[pixelPos] = float4(skyMV, 0);

    // Accumulate sky radiance as stable (noise-free) radiance
    ctx.AccumulateStableRadiance(pixelPos, skyRadiance * throughput);
}

// ============================================================================
// StablePlanesHandleHit — Delta surface hit during BUILD pass
// ============================================================================
// At each delta surface, we:
// 1. Evaluate delta lobes via EvalDeltaLobes
// 2. If the surface has non-delta components, this is the base surface → store plane
// 3. If purely delta: the primary lobe continues, others fork to empty planes
// 4. Accumulate emissive to StableRadiance
//
// Returns: true if the path should continue tracing (delta-only surface),
//          false if the path terminates here (base surface stored, or vertex limit)

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
    const bool isDominant)
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

    // Compute motion vector for this hit surface
    // virtualWorldPos is the point along the camera ray at accumulated scene distance
    float3 virtualWorldPos = rayOrigin + rayDir * totalSceneLength;
    // No per-object motion currently, MV is purely from camera movement
    float3 computedMV = computeMotionVector(virtualWorldPos, virtualWorldPos);

    // Accumulate emissive along the delta path (this is noise-free, deterministic)
    float3 emissive = surface.Emissive;
    if (any(emissive > 0))
        ctx.AccumulateStableRadiance(pixelPos, emissive * throughput);

    // Check if we've exceeded vertex depth limit
    if (vertexIndex >= ctx.maxStablePlaneVertexDepth)
    {
        // Store the current surface as the plane base (even if delta)
        float3 faceNormal = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0 ? surface.FaceNormal : -surface.FaceNormal;
        ctx.StoreStablePlane(
            pixelPos, planeIndex, vertexIndex,
            rayOrigin, rayDir, stableBranchID,
            totalSceneLength, hitDistance,
            throughput, computedMV,
            surface.Roughness, surface.Normal,
            surface.DiffuseAlbedo.xxx, surface.F0,
            isDominant, 0, 0
        );
        if (isDominant)
            MotionVectors[pixelPos] = float4(computedMV, 0);
        return result;
    }

    // Evaluate delta lobes
    DeltaLobe deltaLobes[cMaxDeltaLobes];
    int deltaLobeCount;
    float nonDeltaPart;
    bsdf.EvalDeltaLobes(brdfContext, surface, deltaLobes, deltaLobeCount, nonDeltaPart);

    // Filter very weak lobes
    for (int k = 0; k < deltaLobeCount; k++)
    {
        if (Average3(abs(deltaLobes[k].thp)) < 0.001)
            deltaLobes[k].thp = 0;
    }

    // Count active delta lobes
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

    // Determine face normal orientation
    float3 faceNormal = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.0 ? surface.FaceNormal : -surface.FaceNormal;

    // If there's a non-delta component, this is the base surface for the plane
    // (The first non-delta bounce is where the FILL pass will start stochastic tracing)
    if (nonDeltaPart > 1e-5 || activeDeltaLobes == 0)
    {
        // Compute BSDF estimates for the denoiser
        float3 diffBSDFEstimate = max(surface.DiffuseAlbedo, 0.04);
        float3 specBSDFEstimate = max(surface.F0, 0.04);

        ctx.StoreStablePlane(
            pixelPos, planeIndex, vertexIndex,
            rayOrigin, rayDir, stableBranchID,
            totalSceneLength, hitDistance,
            throughput, computedMV,
            surface.Roughness, surface.Normal,
            diffBSDFEstimate, specBSDFEstimate,
            isDominant, 0, 0
        );
        if (isDominant)
            MotionVectors[pixelPos] = float4(computedMV, 0);
        return result;
    }

    // Pure delta surface: fork paths for each active lobe
    // The first active lobe continues on the current path (reuse the current planeIndex)
    // Additional lobes get forked to empty planes

    // Find empty planes for forking
    int availableCount = 0;
    int availablePlanes[cStablePlaneCount];
    ctx.GetAvailableEmptyPlanes(pixelPos, availableCount, availablePlanes);

    int forkedCount = 0;

    for (int lobeIdx = 0; lobeIdx < deltaLobeCount; lobeIdx++)
    {
        if (!any(deltaLobes[lobeIdx].thp > 0))
            continue;

        if (lobeIdx == firstActiveLobe)
        {
            // First active lobe: continue on current path
            result.continueTracing      = true;
            result.nextRayDir           = deltaLobes[lobeIdx].dir;
            result.nextRayOrigin        = OffsetRay(surface.Position, faceNormal, deltaLobes[lobeIdx].transmission != 0);
            result.nextThp              = throughput * deltaLobes[lobeIdx].thp;
            result.nextBranchID         = StablePlanesAdvanceBranchID(stableBranchID, lobeIdx);
            result.nextVertexIndex      = vertexIndex + 1;
            result.nextImageXform       = UpdateImageXform(imageXform, -rayDir, deltaLobes[lobeIdx].dir, faceNormal, deltaLobes[lobeIdx].transmission != 0);
            result.nextRoughnessAccum   = roughnessAccum;
        }
        else
        {
            // Additional lobes: fork to empty plane if available
            if (forkedCount < availableCount)
            {
                int targetPlane = availablePlanes[forkedCount];
                forkedCount++;

                StablePlaneExplorationPayload forkPayload = SplitDeltaPath(
                    surface.Position, faceNormal, deltaLobes[lobeIdx], lobeIdx,
                    stableBranchID, vertexIndex, throughput, motionVectors,
                    totalSceneLength, imageXform, roughnessAccum, -rayDir
                );

                uint4 packed[5];
                forkPayload.Pack(packed);
                ctx.StoreExplorationStart(pixelPos, targetPlane, packed);
            }
            // else: no more free planes, discard this lobe
        }
    }

    // If no lobe continued (shouldn't happen, but safety), store as base surface
    if (!result.continueTracing)
    {
        ctx.StoreStablePlane(
            pixelPos, planeIndex, vertexIndex,
            rayOrigin, rayDir, stableBranchID,
            totalSceneLength, hitDistance,
            throughput, computedMV,
            surface.Roughness, surface.Normal,
            max(surface.DiffuseAlbedo, 0.04), max(surface.F0, 0.04),
            isDominant, 0, 0
        );
        if (isDominant)
            MotionVectors[pixelPos] = float4(computedMV, 0);
    }

    return result;
}

#endif // PATH_TRACER_MODE_BUILD_STABLE_PLANES

// ============================================================================
// FILL Pass Helpers
// ============================================================================

#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES

// Flags for the FILL pass path state
static const uint kStablePlaneFlag_OnPlane          = 1 << 0;  // Currently on a stable plane base surface
static const uint kStablePlaneFlag_OnBranch         = 1 << 1;  // On a stable delta branch (emissive was captured in BUILD)
static const uint kStablePlaneFlag_OnDominant       = 1 << 2;  // On the dominant plane's branch
static const uint kStablePlaneFlag_BaseScatterDiff  = 1 << 3;  // First scatter off plane base was diffuse

struct StablePlaneFillState
{
    uint    stableBranchID;
    uint    planeIndex;
    uint    flags;
    uint    bouncesFromPlane;

    bool hasFlag(uint f)    { return (flags & f) != 0; }
    void setFlag(uint f, bool v) { if(v) flags |= f; else flags &= ~f; }
};

// Restore path state from the stable plane buffer for the FILL pass.
// This loads plane 0 (the primary plane) and reconstructs the path origin/direction/throughput.
// Returns tMinMax for the narrow re-trace window.
float2 FirstHitFromVBuffer(
    inout StablePlaneFillState fillState,
    inout float3 rayOrigin,
    inout float3 rayDir,
    inout float3 throughput,
    out float sceneLength,
    out uint vertexIndex,
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

    bool isMiss = !isfinite(sceneLength);

    if (!isMiss)
    {
        // Narrow the ray interval for cheap re-hit
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

    return isMiss ? float2(-1, -1) : tMinMax;  // return negative tMinMax to signal miss
}

// Called after each BSDF scatter during the FILL pass.
// Updates branch tracking, commits radiance on plane switches.
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

    // Update branch ID and check if still on a known delta path
    if (fillState.hasFlag(kStablePlaneFlag_OnBranch) && nextVertexIndex <= cStablePlaneMaxVertexIndex)
    {
        fillState.stableBranchID = StablePlanesAdvanceBranchID(fillState.stableBranchID, bsdfSample.getDeltaLobeIndex());

        bool onStablePath = false;
        for (uint spi = 0; spi < cStablePlaneCount; spi++)
        {
            uint planeBranchID = ctx.GetBranchID(pixelPos, spi);
            if (planeBranchID == cStablePlaneInvalidBranchID)
                continue;

            // Exact match: we've arrived at this stable plane
            if (StablePlaneIsOnPlane(planeBranchID, fillState.stableBranchID))
            {
                // Commit accumulated radiance to previous plane
                ctx.CommitDenoiserRadiance(pixelPos, fillState.planeIndex, pathL);

                fillState.planeIndex = spi;
                fillState.setFlag(kStablePlaneFlag_OnDominant, spi == ctx.LoadDominantIndex(pixelPos));
                fillState.setFlag(kStablePlaneFlag_OnPlane, true);
                fillState.bouncesFromPlane = 0;
                onStablePath = true;
                break;
            }

            // Prefix match: on the path leading to this plane
            uint planeVertexIndex = StablePlanesVertexIndexFromBranchID(planeBranchID);
            onStablePath |= StablePlaneIsOnStablePath(planeBranchID, planeVertexIndex, fillState.stableBranchID, nextVertexIndex);
        }
        fillState.setFlag(kStablePlaneFlag_OnBranch, onStablePath);
    }
    else
    {
        // Fell off the stable path
        fillState.stableBranchID = cStablePlaneInvalidBranchID;
        fillState.setFlag(kStablePlaneFlag_OnBranch, false);
        fillState.bouncesFromPlane++;
    }

    if (!fillState.hasFlag(kStablePlaneFlag_OnPlane))
        fillState.bouncesFromPlane++;
}

#endif // FILL

#endif // __PATH_TRACER_STABLE_PLANES_HLSLI__
