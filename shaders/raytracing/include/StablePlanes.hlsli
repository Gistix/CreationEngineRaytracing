/*
 * Stable Planes - Path decomposition for improved denoising of delta surfaces (mirrors, glass).
 * 
 * Based on ideas from:
 * - "Path-space Motion Estimation and Decomposition for Robust Animation Filtering" (Mitsuba)
 * - "Rendering Perfect Reflections and Refractions in Path-Traced Games" (NVIDIA)
 * - RTXPT reference implementation
 *
 * Each pixel is decomposed into up to cStablePlaneCount (3) independent "stable planes",
 * each with its own GBuffer data (normal, roughness, motion vectors) and noisy radiance.
 * This allows per-plane denoising with correct temporal tracking.
 *
 * Two-pass architecture:
 *   BUILD pass: Deterministic delta path exploration, assigns planes
 *   FILL pass:  Standard stochastic path tracing from plane base surfaces
 */

#ifndef __STABLE_PLANES_HLSLI__
#define __STABLE_PLANES_HLSLI__

// ============================================================================
// Constants
// ============================================================================

#define PATH_TRACER_MODE_REFERENCE              0
#define PATH_TRACER_MODE_BUILD_STABLE_PLANES    1
#define PATH_TRACER_MODE_FILL_STABLE_PLANES     2

#ifndef PATH_TRACER_MODE
#define PATH_TRACER_MODE PATH_TRACER_MODE_REFERENCE
#endif

static const uint   cStablePlaneCount               = 3;            // max planes per pixel
static const uint   cStablePlaneMaxVertexIndex       = 15;           // max delta path depth (keeps branchID in 32 bits)
static const uint   cStablePlaneInvalidBranchID      = 0xFFFFFFFF;   // empty/invalid plane
static const uint   cStablePlaneEnqueuedBranchID     = 0xFFFFFFFE;   // enqueued for exploration
static const uint   cStablePlaneJustStartedID        = 0;            // currently being explored
static const float  kMaxRayTravel                    = 1e15f;
static const float  kEnvironmentMapSceneDistance      = 50000.0f;
static const float  kSpecHeuristicBoost              = 1.0f;

// ============================================================================
// Packing Helpers (fp16 ↔ fp32)
// ============================================================================

#ifndef HLF_MAX
#define HLF_MAX 65504.0f
#endif

uint3 PackTwoFp32ToFp16(float3 a, float3 b)
{
    return (f32tof16(clamp(a, -HLF_MAX, HLF_MAX)) << 16) | f32tof16(clamp(b, -HLF_MAX, HLF_MAX));
}

void UnpackTwoFp32ToFp16(uint3 packed, out float3 a, out float3 b)
{
    a = f16tof32(packed >> 16);
    b = f16tof32(packed & 0xFFFF);
}

uint Fp32ToFp16_2(float2 v)
{
    const uint2 r = f32tof16(clamp(v, -HLF_MAX, HLF_MAX));
    return (r.y << 16) | (r.x & 0xFFFF);
}

float2 Fp16ToFp32_2(uint r)
{
    uint2 v;
    v.x = (r & 0xFFFF);
    v.y = (r >> 16);
    return f16tof32(v);
}

uint2 Fp32ToFp16(float4 v)
{
    const uint d0 = Fp32ToFp16_2(v.xy);
    const uint d1 = Fp32ToFp16_2(v.zw);
    return uint2(d0, d1);
}

float4 Fp16ToFp32(uint2 d)
{
    const float2 d0 = Fp16ToFp32_2(d.x);
    const float2 d1 = Fp16ToFp32_2(d.y);
    return float4(d0.xy, d1.xy);
}

// ============================================================================
// Octahedral Normal Encoding
// ============================================================================

float2 OctWrap(float2 v)
{
    return (1.0 - abs(v.yx)) * (v.xy >= 0.0 ? 1.0.xx : -1.0.xx);
}

float2 OctEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

float3 OctDecode(float2 f)
{
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0 ? -t.xx : t.xx);
    return normalize(n);
}

uint NDirToOctUnorm32(float3 n)
{
    float2 p = OctEncode(n);
    return uint(saturate(p.x) * 0xFFFE) | (uint(saturate(p.y) * 0xFFFE) << 16);
}

float3 OctToNDirUnorm32(uint pUnorm)
{
    float2 p;
    p.x = saturate(float(pUnorm & 0xFFFF) / float(0xFFFE));
    p.y = saturate(float(pUnorm >> 16) / float(0xFFFE));
    return OctDecode(p);
}

// ============================================================================
// Utility
// ============================================================================

float Average3(float3 rgb) { return (rgb.x + rgb.y + rgb.z) / 3.0; }

// Rotation matrix from direction A to direction B (both must be normalized)
float3x3 MatrixRotateFromTo(float3 from, float3 to)
{
    float3 v = cross(from, to);
    float c = dot(from, to);
    float k = 1.0 / (1.0 + c + 1e-7);
    return float3x3(
        v.x * v.x * k + c,     v.x * v.y * k - v.z,   v.x * v.z * k + v.y,
        v.y * v.x * k + v.z,   v.y * v.y * k + c,      v.y * v.z * k - v.x,
        v.z * v.x * k - v.y,   v.z * v.y * k + v.x,    v.z * v.z * k + c
    );
}

// Reinhard tone mapping for guide albedo estimation
float3 ReinhardMax(float3 color) { float m = max(max(color.r, color.g), color.b); return (m > 0) ? color / (1.0 + m) : 0.xxx; }

// ============================================================================
// StablePlane Data Structure (80 bytes = 20 dwords)
// ============================================================================

struct StablePlane
{
    float3  RayOrigin;                      // Ray origin for re-trace; inf = sky miss
    float   LastRayTCurrent;                // Last segment length for narrow re-trace window
    float3  RayDir;                         // Ray direction
    float   SceneLength;                    // Total ray travel distance; inf = sky miss
    uint3   PackedThpAndMVs;                // [fp16] throughput (high) + motion vectors (low)
    uint    VertexIndexAndRoughness;         // [16:16] vertex index (high) + fp16 roughness (low)
    uint3   DenoiserPackedBSDFEstimate;      // [fp16] diff BSDF estimate (high 3) + spec estimate (low 3)
    uint    PackedNormal;                    // Octahedral-encoded normal
    uint2   PackedNoisyRadianceAndSpecAvg;   // [fp16] noisy radiance RGB + specular average
    uint    FlagsAndVertexIndex;             // Path restart flags
    uint    PackedCounters;                  // Path restart counters

    // --- Accessors ---
    bool    IsEmpty()                   { return (VertexIndexAndRoughness >> 16) == 0; }
    float3  GetNormal()                 { return OctToNDirUnorm32(PackedNormal); }
    float   GetRoughness()              { return f16tof32(VertexIndexAndRoughness & 0xFFFF); }
    uint    GetVertexIndex()            { return VertexIndexAndRoughness >> 16; }

    float3  GetNoisyRadiance()          { return Fp16ToFp32(PackedNoisyRadianceAndSpecAvg).xyz; }
    float4  GetNoisyRadianceAndSpecRA() { return Fp16ToFp32(PackedNoisyRadianceAndSpecAvg).xyzw; }

    // Diff/spec split heuristic: uses specular average fraction
    float3  GetNoisyDiffRadiance()
    {
        float4 l = Fp16ToFp32(PackedNoisyRadianceAndSpecAvg);
        float totalAvg = Average3(l.rgb);
        return l.rgb * saturate(1.0 - (l.a * kSpecHeuristicBoost) / (totalAvg + 1e-12)).xxx;
    }
    float3  GetNoisySpecRadiance()
    {
        float4 l = Fp16ToFp32(PackedNoisyRadianceAndSpecAvg);
        float totalAvg = Average3(l.rgb);
        return l.rgb * saturate((l.a * kSpecHeuristicBoost) / (totalAvg + 1e-12)).xxx;
    }

#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    // Reuse StablePlane storage as custom payload for enqueued exploration paths
    void PackCustomPayload(const uint4 packed[5])
    {
        RayOrigin                       = asfloat(packed[0].xyz);
        LastRayTCurrent                 = asfloat(packed[0].w);
        RayDir                          = asfloat(packed[1].xyz);
        SceneLength                     = asfloat(packed[1].w);
        PackedThpAndMVs                 = packed[2].xyz;
        VertexIndexAndRoughness         = packed[2].w;
        DenoiserPackedBSDFEstimate      = packed[3].xyz;
        PackedNormal                    = packed[3].w;
        PackedNoisyRadianceAndSpecAvg   = packed[4].xy;
        FlagsAndVertexIndex             = packed[4].z;
        PackedCounters                  = packed[4].w;
    }
    void UnpackCustomPayload(inout uint4 packed[5])
    {
        packed[0].xyz = asuint(RayOrigin);
        packed[0].w   = asuint(LastRayTCurrent);
        packed[1].xyz = asuint(RayDir);
        packed[1].w   = asuint(SceneLength);
        packed[2].xyz = PackedThpAndMVs;
        packed[2].w   = VertexIndexAndRoughness;
        packed[3].xyz = DenoiserPackedBSDFEstimate;
        packed[3].w   = PackedNormal;
        packed[4].xy  = PackedNoisyRadianceAndSpecAvg;
        packed[4].z   = FlagsAndVertexIndex;
        packed[4].w   = PackedCounters;
    }
#endif
};

// ============================================================================
// Branch ID Functions
// ============================================================================

// Advance branch ID at each delta bounce; deltaLobeID must be < 4
uint StablePlanesAdvanceBranchID(const uint prevStableBranchID, const uint deltaLobeID)
{
    return (prevStableBranchID << 2) | deltaLobeID;
}

uint StablePlanesGetParentLobeID(const uint stableBranchID)
{
    return stableBranchID & 0x3;
}

// Compute vertex index from branch ID (initial camera = 1)
uint StablePlanesVertexIndexFromBranchID(const uint stableBranchID)
{
    return firstbithigh(stableBranchID) / 2 + 1;
}

// Exact match: vertex is AT this plane's base surface
bool StablePlaneIsOnPlane(const uint planeBranchID, const uint vertexBranchID)
{
    return planeBranchID == vertexBranchID;
}

// Prefix match: vertex is ON the path leading to this plane (not yet reached base)
bool StablePlaneIsOnStablePath(const uint planeBranchID, const uint planeVertexIndex, const uint vertexBranchID, const uint vertexIndex)
{
    if (vertexIndex > planeVertexIndex)
        return false;
    return (planeBranchID >> ((planeVertexIndex - vertexIndex) * 2)) == vertexBranchID;
}

bool StablePlaneIsOnStablePath(const uint planeBranchID, const uint vertexBranchID)
{
    return StablePlaneIsOnStablePath(planeBranchID, StablePlanesVertexIndexFromBranchID(planeBranchID), vertexBranchID, StablePlanesVertexIndexFromBranchID(vertexBranchID));
}

float3 StablePlaneDebugVizColor(const uint planeIndex)
{
    return float3(planeIndex == 0 || planeIndex == 3, planeIndex == 1, planeIndex == 2 || planeIndex == 3);
}

// ============================================================================
// StablePlanesContext — Buffer management and operations
// ============================================================================

struct StablePlanesContext
{
    RWTexture2D<float4>                 StableRadianceUAV;
    RWTexture2DArray<uint>              StablePlanesHeaderUAV;      // [0,1,2] = BranchIDs, [3] = firstHitRayLength | dominantIndex
    RWStructuredBuffer<StablePlane>     StablePlanesUAV;

    uint    renderWidth;
    uint    renderHeight;
    uint    activeStablePlaneCount;
    uint    maxStablePlaneVertexDepth;

    static StablePlanesContext make(
        RWTexture2DArray<uint> headerUAV,
        RWStructuredBuffer<StablePlane> bufferUAV,
        RWTexture2D<float4> radianceUAV,
        uint width, uint height, uint activePlaneCount, uint maxVertexDepth)
    {
        StablePlanesContext ctx;
        ctx.StablePlanesHeaderUAV   = headerUAV;
        ctx.StablePlanesUAV         = bufferUAV;
        ctx.StableRadianceUAV       = radianceUAV;
        ctx.renderWidth             = width;
        ctx.renderHeight            = height;
        ctx.activeStablePlaneCount  = activePlaneCount;
        ctx.maxStablePlaneVertexDepth = min(maxVertexDepth, cStablePlaneMaxVertexIndex);
        return ctx;
    }

    // --- Addressing ---
    uint PixelToAddress(uint2 pixelPos, uint planeIndex)
    {
        return pixelPos.y * renderWidth + pixelPos.x + planeIndex * renderWidth * renderHeight;
    }
    uint PixelToAddress(uint2 pixelPos) { return PixelToAddress(pixelPos, 0); }

    // --- Load/Store StablePlane ---
    StablePlane LoadStablePlane(const uint2 pixelPos, const uint planeIndex)
    {
        return StablePlanesUAV[PixelToAddress(pixelPos, planeIndex)];
    }

    static void UnpackStablePlane(const StablePlane sp, out uint vertexIndex, out float3 rayOrigin, out float3 rayDir, out float sceneLength, out float3 thp, out float3 motionVectors)
    {
        vertexIndex = sp.VertexIndexAndRoughness >> 16;
        rayOrigin   = sp.RayOrigin;
        sceneLength = sp.SceneLength;
        rayDir      = sp.RayDir;
        UnpackTwoFp32ToFp16(sp.PackedThpAndMVs, thp, motionVectors);
    }

    void LoadStablePlane(const uint2 pixelPos, const uint planeIndex, out uint vertexIndex, out float3 rayOrigin, out float3 rayDir,
                         out uint stableBranchID, out float sceneLength, out float3 thp, out float3 motionVectors)
    {
        stableBranchID = GetBranchID(pixelPos, planeIndex);
        UnpackStablePlane(LoadStablePlane(pixelPos, planeIndex), vertexIndex, rayOrigin, rayDir, sceneLength, thp, motionVectors);
    }

    // --- Branch ID ---
    uint GetBranchID(const uint2 pixelPos, const uint planeIndex)
    {
        return StablePlanesHeaderUAV[uint3(pixelPos, planeIndex)];
    }
    void SetBranchID(const uint2 pixelPos, const uint planeIndex, uint stableBranchID)
    {
        StablePlanesHeaderUAV[uint3(pixelPos, planeIndex)] = stableBranchID;
    }

    // --- First Hit Ray Length + Dominant Index (packed in Header slice 3) ---
    void StoreFirstHitRayLengthAndClearDominantToZero(uint2 pixelPos, float length)
    {
        StablePlanesHeaderUAV[uint3(pixelPos, 3)] = asuint(min(kMaxRayTravel, length)) & 0xFFFFFFFC;
    }
    float LoadFirstHitRayLength(uint2 pixelPos)
    {
        return asfloat(StablePlanesHeaderUAV[uint3(pixelPos, 3)] & 0xFFFFFFFC);
    }
    void StoreDominantIndex(uint2 pixelPos, uint index)
    {
        StablePlanesHeaderUAV[uint3(pixelPos, 3)] = (StablePlanesHeaderUAV[uint3(pixelPos, 3)] & 0xFFFFFFFC) | (0x3 & index);
    }
    uint LoadDominantIndex(uint2 pixelPos)
    {
        return StablePlanesHeaderUAV[uint3(pixelPos, 3)] & 0x3;
    }

    // --- Stable Radiance (noise-free emissive along delta paths) ---
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    void StoreStableRadiance(uint2 pixelPos, float3 radiance)       { StableRadianceUAV[pixelPos].xyzw = float4(clamp(radiance, 0, HLF_MAX), 0); }
    void AccumulateStableRadiance(uint2 pixelPos, float3 radiance)  { StableRadianceUAV[pixelPos].xyz += radiance; }
#endif
    float3 LoadStableRadiance(uint2 pixelPos) { return StableRadianceUAV[pixelPos].xyz; }

    // --- Per-pixel initialization (called at start of BUILD pass) ---
    void StartPixel(uint2 pixelPos)
    {
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
        StoreStableRadiance(pixelPos, 0.xxx);
        StablePlanesHeaderUAV[uint3(pixelPos, 0)] = cStablePlaneInvalidBranchID;
        StablePlanesHeaderUAV[uint3(pixelPos, 1)] = cStablePlaneInvalidBranchID;
        StablePlanesHeaderUAV[uint3(pixelPos, 2)] = cStablePlaneInvalidBranchID;
#endif
    }

    // --- BUILD pass: Store plane base surface ---
#if PATH_TRACER_MODE == PATH_TRACER_MODE_BUILD_STABLE_PLANES
    void StoreStablePlane(const uint2 pixelPos, const uint planeIndex, const uint vertexIndex,
                          const float3 rayOrigin, const float3 rayDir, const uint stableBranchID,
                          const float sceneLength, const float rayTCurrent,
                          const float3 thp, const float3 motionVectors,
                          const float roughness, const float3 worldNormal,
                          const float3 diffBSDFEstimate, const float3 specBSDFEstimate,
                          bool dominantSP, uint flagsAndVertexIndex, uint packedCounters)
    {
        uint address = PixelToAddress(pixelPos, planeIndex);
        StablePlane sp;
        sp.RayOrigin                = rayOrigin;
        sp.RayDir                   = rayDir;
        sp.SceneLength              = sceneLength;
        sp.VertexIndexAndRoughness  = (vertexIndex << 16) | f32tof16(roughness);
        sp.PackedThpAndMVs          = PackTwoFp32ToFp16(thp, motionVectors);

        const float kNRDMinReflectance = 0.04f;
        const float kNRDMaxReflectance = HLF_MAX;
        float3 fullDiffBSDFEstimate = clamp(diffBSDFEstimate, kNRDMinReflectance.xxx, kNRDMaxReflectance.xxx);
        float3 fullSpecBSDFEstimate = clamp(specBSDFEstimate, kNRDMinReflectance.xxx, kNRDMaxReflectance.xxx);

        sp.DenoiserPackedBSDFEstimate       = PackTwoFp32ToFp16(fullDiffBSDFEstimate, fullSpecBSDFEstimate);
        sp.PackedNormal                     = NDirToOctUnorm32(worldNormal);
        sp.PackedNoisyRadianceAndSpecAvg    = Fp32ToFp16(float4(0, 0, 0, 0));
        sp.LastRayTCurrent                  = rayTCurrent;
        sp.FlagsAndVertexIndex              = flagsAndVertexIndex;
        sp.PackedCounters                   = packedCounters;
        StablePlanesUAV[address]            = sp;
        SetBranchID(pixelPos, planeIndex, stableBranchID);

        if (dominantSP && planeIndex != 0)
            StoreDominantIndex(pixelPos, planeIndex);
    }

    // --- Enqueue forked delta path for later exploration ---
    void StoreExplorationStart(uint2 pixelPos, uint planeIndex, const uint4 pathPayload[5])
    {
        uint address = PixelToAddress(pixelPos, planeIndex);
        StablePlane sp;
        sp.PackCustomPayload(pathPayload);
        StablePlanesUAV[address] = sp;
        SetBranchID(pixelPos, planeIndex, cStablePlaneEnqueuedBranchID);
    }

    // --- Restore enqueued path for exploration ---
    void ExplorationStart(uint2 pixelPos, uint planeIndex, inout uint4 pathPayload[5])
    {
        uint address = PixelToAddress(pixelPos, planeIndex);
        StablePlane sp = StablePlanesUAV[address];
        sp.UnpackCustomPayload(pathPayload);
        SetBranchID(pixelPos, planeIndex, cStablePlaneJustStartedID);
    }

    // --- Find next enqueued plane for exploration ---
    int FindNextToExplore(uint2 pixelPos, uint fromPlane)
    {
        for (int i = fromPlane; i < (int)cStablePlaneCount; i++)
            if (GetBranchID(pixelPos, i) == cStablePlaneEnqueuedBranchID)
                return i;
        return -1;
    }

    // --- Get available empty planes for forking ---
    void GetAvailableEmptyPlanes(const uint2 pixelPos, inout int availableCount, inout int availablePlanes[cStablePlaneCount])
    {
        availableCount = 0;
        for (int i = 1; i < (int)min(activeStablePlaneCount, cStablePlaneCount); i++)
            if (GetBranchID(pixelPos, i) == cStablePlaneInvalidBranchID)
                availablePlanes[availableCount++] = i;
    }
#endif // BUILD

    // --- FILL pass: Commit accumulated radiance to plane ---
#if PATH_TRACER_MODE == PATH_TRACER_MODE_FILL_STABLE_PLANES
    void CommitDenoiserRadiance(uint2 pixelPos, uint planeIndex, inout float4 pathL)
    {
        uint address = PixelToAddress(pixelPos, planeIndex);

        float4 accumRadiance = pathL;
        const uint2 existingRadiancePacked = StablePlanesUAV[address].PackedNoisyRadianceAndSpecAvg;
        if (existingRadiancePacked.x != 0 || existingRadiancePacked.y != 0)
        {
            float4 existingRadiance = Fp16ToFp32(existingRadiancePacked);
            accumRadiance.rgba += existingRadiance.rgba;
        }
        StablePlanesUAV[address].PackedNoisyRadianceAndSpecAvg = Fp32ToFp16(accumRadiance);

        pathL = float4(0, 0, 0, 0);
    }
#endif // FILL

    // --- Get all radiance (stable + noisy from all planes) ---
    float3 GetAllRadiance(uint2 pixelPos, bool includeNoisy)
    {
        float3 pathL = LoadStableRadiance(pixelPos);
        if (includeNoisy)
        {
            for (int i = 0; i < (int)cStablePlaneCount; i++)
            {
                if (GetBranchID(pixelPos, i) == cStablePlaneInvalidBranchID)
                    continue;
                pathL += StablePlanesUAV[PixelToAddress(pixelPos, i)].GetNoisyRadiance();
            }
        }
        return pathL;
    }
};

#endif // __STABLE_PLANES_HLSLI__