/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#include "GI12Registers.hlsli"
#include "GI12Common.hlsli"

// Screen Probes Buffers
RWTexture2D<uint>         g_ScreenProbes_ProbeMaskBuffer          : register(u0);
RWTexture2D<float4>       g_ScreenProbes_ProbeBuffer              : register(u1);
RWStructuredBuffer<uint2> g_ScreenProbes_ProbeSHBuffer            : register(u2);
RWStructuredBuffer<uint>  g_ScreenProbes_ProbeSpawnBuffer         : register(u3);
RWStructuredBuffer<uint2> g_ScreenProbes_ProbeSpawnSampleBuffer   : register(u4);
RWStructuredBuffer<uint2> g_ScreenProbes_ProbeSpawnRadianceBuffer : register(u5);
RWStructuredBuffer<uint>  g_ScreenProbes_EmptyTileBuffer          : register(u6);
RWStructuredBuffer<uint>  g_ScreenProbes_EmptyTileCountBuffer     : register(u7);
RWStructuredBuffer<uint>  g_ScreenProbes_ProbeCachedTileLRUBuffer : register(u8);
RWTexture2D<float4>       g_IrradianceBuffer                      : register(u9);
RWTexture2D<float4>       g_GIDenoiser_ColorBuffer                : register(u10);
RWStructuredBuffer<uint>  g_GlossyReflections_RtSampleBuffer      : register(u11);
RWStructuredBuffer<uint>  g_GlossyReflections_RtSampleCountBuffer : register(u12);

Texture2D<uint>           g_ScreenProbes_PreviousProbeMaskBuffer  : register(t11);
Texture2D<float4>         g_ScreenProbes_PreviousProbeBuffer      : register(t12);
StructuredBuffer<uint2>   g_ScreenProbes_PreviousProbeSHBuffer    : register(t13);

groupshared float lds_RadianceScan[64];

// Convert screen uv and depth to world space position
float3 ScreenToWorld(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(GI12CB.view_proj_inv, clip);
    return world.xyz / world.w + GI12CB.eye;
}

float3 ScreenToWorldPrev(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(GI12CB.view_proj_inv_prev, clip);
    return world.xyz / world.w + GI12CB.eye_prev;
}

[numthreads(8, 8, 1)]
void ClearProbeMask(uint2 did : SV_DispatchThreadID)
{
    if (any(did >= ScreenProbesCB.probe_count))
        return;

    g_ScreenProbes_ProbeMaskBuffer[did] = kGI1_InvalidId;
}

[numthreads(64, 1, 1)]
void InitCachedTileLRU(uint did : SV_DispatchThreadID)
{
    uint maxTiles = ScreenProbesCB.probe_count.x * ScreenProbesCB.probe_count.y;
    if (did >= maxTiles)
        return;

    g_ScreenProbes_ProbeCachedTileLRUBuffer[did] = did;
}

[numthreads(8, 8, 1)]
void ReprojectScreenProbes(uint2 did : SV_DispatchThreadID)
{
    if (GI12CB.use_temporal_feedback == 0)
        return;

    if (any(did >= ScreenProbesCB.probe_count))
        return;

    uint2 pixelPos = did * ScreenProbesCB.probe_size + (ScreenProbesCB.probe_size / 2);
    if (any(pixelPos >= GI12CB.buffer_dimensions))
        return;

    float depth = Depth.Load(int3(pixelPos, 0));
    if (depth >= 1.0f)
        return; // sky

    float2 uv = (float2(pixelPos) + 0.5f) / float2(GI12CB.buffer_dimensions);
    float2 motion = MotionVectors.SampleLevel(PointClampSampler, uv, 0);
    float2 prevUV = uv - motion;

    if (any(prevUV < 0.0f) || any(prevUV > 1.0f))
        return; // disoccluded

    uint2 prevTile = uint2(prevUV * float2(GI12CB.buffer_dimensions)) / ScreenProbesCB.probe_size;
    if (any(prevTile >= ScreenProbesCB.probe_count))
        return;

    uint prevProbe = g_ScreenProbes_PreviousProbeMaskBuffer[prevTile];
    if (prevProbe != kGI1_InvalidId && prevProbe != 0)
    {
        g_ScreenProbes_ProbeMaskBuffer[did] = prevProbe;
        for (int py = 0; py < 8; ++py)
        {
            for (int px = 0; px < 8; ++px)
            {
                float4 prevRad = g_ScreenProbes_PreviousProbeBuffer.Load(int3(prevTile * 8 + int2(px, py), 0));
                g_ScreenProbes_ProbeBuffer[did * 8 + int2(px, py)] = prevRad;
            }
        }
    }
}

[numthreads(8, 8, 1)]
void CountAndScatterProbes(uint2 did : SV_DispatchThreadID)
{
    if (any(did >= ScreenProbesCB.probe_count))
        return;

    uint2 pixelPos = did * ScreenProbesCB.probe_size + (ScreenProbesCB.probe_size / 2);
    if (any(pixelPos >= GI12CB.buffer_dimensions))
        return;

    float depth = Depth.Load(int3(pixelPos, 0));
    if (depth >= 1.0f)
        return; // skip sky tiles

    uint probe = g_ScreenProbes_ProbeMaskBuffer[did];
    if (probe == kGI1_InvalidId)
    {
        uint emptyIndex;
        InterlockedAdd(g_ScreenProbes_EmptyTileCountBuffer[0], 1, emptyIndex);
        uint tileID = did.x + did.y * ScreenProbesCB.probe_count.x;
        g_ScreenProbes_EmptyTileBuffer[emptyIndex] = tileID;
    }
}

[numthreads(64, 1, 1)]
void SpawnScreenProbes(uint did : SV_DispatchThreadID)
{
    uint emptyCount = g_ScreenProbes_EmptyTileCountBuffer[0];
    if (did >= emptyCount)
        return;

    uint tileID = g_ScreenProbes_EmptyTileBuffer[did];
    uint2 tile = uint2(tileID % ScreenProbesCB.probe_count.x, tileID / ScreenProbesCB.probe_count.x);
    uint2 seed = tile * ScreenProbesCB.probe_size + (ScreenProbesCB.probe_size / 2);

    g_ScreenProbes_ProbeSpawnBuffer[did] = ScreenProbes_PackSeed(seed);
    g_ScreenProbes_ProbeMaskBuffer[tile] = ScreenProbes_PackSeed(seed);
}

[numthreads(64, 1, 1)]
void SampleScreenProbes(uint did : SV_DispatchThreadID, uint local_id : SV_GroupThreadID)
{
    uint rayIndex = did % 64;
    uint probeIndex = did / 64;

    uint emptyCount = g_ScreenProbes_EmptyTileCountBuffer[0];
    if (probeIndex >= emptyCount)
        return;

    uint tileID = g_ScreenProbes_EmptyTileBuffer[probeIndex];
    uint2 tile = uint2(tileID % ScreenProbesCB.probe_count.x, tileID / ScreenProbesCB.probe_count.x);
    uint2 seed = tile * ScreenProbesCB.probe_size + (ScreenProbesCB.probe_size / 2);
    seed = min(seed, GI12CB.buffer_dimensions - 1);

    float3 normal = normalize(NormalRoughness.Load(int3(seed, 0)).xyz);
    if (dot(normal, normal) < 0.1f)
        normal = float3(0.0f, 0.0f, 1.0f);

    float3 tangent = normalize(cross(abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f), normal));
    float3 bitangent = cross(normal, tangent);

    float2 u = CalculateHaltonSequence(rayIndex);
    float z = sqrt(max(0.0f, u.x));
    float rad = sqrt(saturate(1.0f - z * z));
    float phi = TWO_PI * u.y;
    float3 dir = normalize(tangent * (rad * cos(phi)) + bitangent * (rad * sin(phi)) + normal * z);

    g_ScreenProbes_ProbeSpawnSampleBuffer[did] = ScreenProbes_PackSample(dir);
}

[numthreads(64, 1, 1)]
void BlendScreenProbes(uint did : SV_DispatchThreadID, uint local_id : SV_GroupThreadID)
{
    uint emptyCount = g_ScreenProbes_EmptyTileCountBuffer[0];
    uint totalRays = emptyCount * 64;
    if (did >= totalRays)
        return;

    float4 radiance = ScreenProbes_UnpackRadiance(g_ScreenProbes_ProbeSpawnRadianceBuffer[did]);
    uint2 cellAndProbe = uint2(did % 64, did / 64);
    uint probeIndex = cellAndProbe.y;

    uint tileID = g_ScreenProbes_EmptyTileBuffer[probeIndex];
    uint2 tileCoord = uint2(tileID % ScreenProbesCB.probe_count.x, tileID / ScreenProbesCB.probe_count.x);

    if (GI12CB.use_temporal_feedback != 0)
    {
        float4 prevRad = g_ScreenProbes_PreviousProbeBuffer.Load(int3(tileCoord * 8 + uint2(cellAndProbe.x % 8, cellAndProbe.x / 8), 0));
        radiance = lerp(radiance, prevRad, 0.85f);
    }

    g_ScreenProbes_ProbeBuffer[tileCoord * 8 + uint2(cellAndProbe.x % 8, cellAndProbe.x / 8)] = radiance;
}

[numthreads(64, 1, 1)]
void ProjectScreenProbes(uint did : SV_DispatchThreadID)
{
    uint probeCount = ScreenProbesCB.probe_count.x * ScreenProbesCB.probe_count.y;
    if (did >= probeCount)
        return;

    uint2 tile = uint2(did % ScreenProbesCB.probe_count.x, did / ScreenProbesCB.probe_count.x);
    uint2 seed = tile * ScreenProbesCB.probe_size + (ScreenProbesCB.probe_size / 2);
    seed = min(seed, GI12CB.buffer_dimensions - 1);

    float3 normal = normalize(NormalRoughness.Load(int3(seed, 0)).xyz);
    if (dot(normal, normal) < 0.1f)
        normal = float3(0.0f, 0.0f, 1.0f);

    float3 tangent = normalize(cross(abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f), normal));
    float3 bitangent = cross(normal, tangent);

    float shCoeffs[9];
    float3 accumSH[9];
    for (int k = 0; k < 9; ++k)
        accumSH[k] = float3(0.0f, 0.0f, 0.0f);

    for (int r = 0; r < 64; ++r)
    {
        uint2 sampleCoord = tile * 8 + uint2(r % 8, r / 8);
        float4 radiance = g_ScreenProbes_ProbeBuffer[sampleCoord];

        float2 u = CalculateHaltonSequence(r);
        float z = sqrt(max(0.0f, u.x));
        float rad = sqrt(saturate(1.0f - z * z));
        float phi = TWO_PI * u.y;
        float3 dir = normalize(tangent * (rad * cos(phi)) + bitangent * (rad * sin(phi)) + normal * z);

        SH_GetCoefficients(dir, shCoeffs);
        for (int m = 0; m < 9; ++m)
        {
            accumSH[m] += shCoeffs[m] * radiance.xyz;
        }
    }

    float weight = TWO_PI / 64.0f;
    for (int j = 0; j < 9; ++j)
    {
        g_ScreenProbes_ProbeSHBuffer[9 * did + j] = ScreenProbes_PackSHColor(float4(accumSH[j] * weight, 1.0f));
    }
}

// Full resolution bilateral interpolation
[numthreads(8, 8, 1)]
void InterpolateScreenProbes(uint2 did : SV_DispatchThreadID)
{
    if (any(did >= GI12CB.buffer_dimensions))
        return;

    float depth = Depth.Load(int3(did, 0));
    if (depth >= 1.0f)
    {
        g_IrradianceBuffer[did] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        g_GIDenoiser_ColorBuffer[did] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float4 normRough = NormalRoughness.Load(int3(did, 0));
    float3 normal = normalize(normRough.xyz);

    float2 uv = (float2(did) + 0.5f) / float2(GI12CB.buffer_dimensions);
    float3 worldPos = ScreenToWorld(uv, depth);

    uint2 tile = did / ScreenProbesCB.probe_size;
    tile = min(tile, ScreenProbesCB.probe_count - 1);

    int2 offsets[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) };
    float4 weights = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);

    float clampedCosineSH[9];
    SH_GetCoefficients_ClampedCosine(normal, clampedCosineSH);

    for (int i = 0; i < 4; ++i)
    {
        int2 sampleTile = clamp(int2(tile) + offsets[i], 0, int2(ScreenProbesCB.probe_count) - 1);
        uint probeIndex = sampleTile.x + sampleTile.y * ScreenProbesCB.probe_count.x;

        uint packedSeed = g_ScreenProbes_ProbeMaskBuffer[sampleTile];
        uint2 probeSeed = (packedSeed == kGI1_InvalidId)
            ? (uint2(sampleTile) * ScreenProbesCB.probe_size + (ScreenProbesCB.probe_size / 2))
            : ScreenProbes_UnpackSeed(packedSeed);
        probeSeed = min(probeSeed, GI12CB.buffer_dimensions - 1);

        float probeDepth = Depth.Load(int3(probeSeed, 0));
        float3 probeNormal = normalize(NormalRoughness.Load(int3(probeSeed, 0)).xyz);
        if (dot(probeNormal, probeNormal) < 0.1f)
            probeNormal = normal;
        if (probeDepth >= 1.0f)
            probeDepth = depth;

        float depthDiff = abs(depth - probeDepth) / max(depth, 1e-4f);
        float normalDiff = max(0.0f, dot(normal, probeNormal));
        float w = max(exp(-depthDiff * 10.0f) * pow(normalDiff, 4.0f), 0.05f);

        float3 probeIrr = float3(0.0f, 0.0f, 0.0f);
        for (int c = 0; c < 9; ++c)
        {
            float4 shCol = ScreenProbes_UnpackSHColor(g_ScreenProbes_ProbeSHBuffer[9 * probeIndex + c]);
            probeIrr += clampedCosineSH[c] * shCol.xyz;
        }

        irradiance += max(0.0f, probeIrr) * w;
        weights[i] = w;
    }

    float sumW = weights.x + weights.y + weights.z + weights.w;
    if (sumW > 1e-5f)
        irradiance /= sumW;

    irradiance = max(0.0f, irradiance);
    g_IrradianceBuffer[did] = float4(irradiance, 1.0f);
    g_GIDenoiser_ColorBuffer[did] = float4(irradiance, 1.0f);

    float roughness = normRough.w;
    if (roughness <= GlossyReflectionsCB.high_roughness_threshold)
    {
        uint2 activeOffset = uint2((GI12CB.frame_index >> 0) & 1, (GI12CB.frame_index >> 1) & 1);
        if (!GlossyReflectionsCB.half_res || all((did & 1) == activeOffset))
        {
            uint rtSampleIndex;
            InterlockedAdd(g_GlossyReflections_RtSampleCountBuffer[0], 1, rtSampleIndex);
            g_GlossyReflections_RtSampleBuffer[rtSampleIndex] = GlossyReflections_PackSample(did);
        }
    }
}
