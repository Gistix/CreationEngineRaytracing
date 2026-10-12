// GI-1.2 PurgeTiles + probe decay (Capsaicin hash_grid_cache.hlsl / gi1.comp, MIT).
// Hash = transient tile hash (uint, 0 = empty). Tiles untouched for
// kHashGridCache_TileDecay (50) frames are evicted: hash cleared, Value and
// scratch cells zeroed. Value buffers carry no exponential decay (source).

#include "interop/CameraData.hlsli"
#include "interop/GI12Data.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<GI12Data> GI12Params : register(b1);
ConstantBuffer<GI12ScreenProbesData> ScreenProbes : register(b2);
ConstantBuffer<GI12HashGridData> HashGrid : register(b3);

RWStructuredBuffer<float4> ProbePosition : register(u0);
RWStructuredBuffer<uint4> ProbeSH : register(u1);
RWStructuredBuffer<float4> ProbeNormal : register(u2);
RWStructuredBuffer<uint> Hash : register(u3);
RWStructuredBuffer<uint> TileLastTouched : register(u5);
RWStructuredBuffer<uint> TileUpdate : register(u6);
RWStructuredBuffer<uint> TileUpdateCount : register(u7);
RWStructuredBuffer<float4> ProbeRadiance : register(u8);
RWStructuredBuffer<uint> ProbeLastUpdated : register(u9);
RWStructuredBuffer<uint2> Value : register(u10);
RWStructuredBuffer<uint2> ValueIndirect : register(u11);
RWStructuredBuffer<uint> UpdateCellValue : register(u12);
RWStructuredBuffer<uint> UpdateCellValueIndirect : register(u13);

static const float GI12_DECAY = 0.995f;
static const uint GI12_TILE_DECAY_FRAMES = 50u;
static const uint GI12_PROBE_DECAY_FRAMES = 50u;

[numthreads(64, 1, 1)]
void Main(uint idx : SV_DispatchThreadID)
{
    // Capsaicin PurgeTiles: evict tiles untouched for 50 frames (with
    // wraparound-safe age). Evicted hash set to 0, cells cleared.
    if (idx < HashGrid.NumTiles && Hash[idx] != 0u) {
        const uint lastTouch = TileLastTouched[idx];
        uint age;
        if (GI12Params.FrameIndex < lastTouch)
            age = (0xFFFFFFFFu - lastTouch) + GI12Params.FrameIndex + 1u;
        else
            age = GI12Params.FrameIndex - lastTouch;
        if (age >= GI12_TILE_DECAY_FRAMES) {
            Hash[idx] = 0u;
            const uint cellBase = idx * HashGrid.NumCellsPerTile;
            [loop]
            for (uint cellOffset = 0u; cellOffset < HashGrid.NumCellsPerTile; ++cellOffset) {
                const uint cell = cellBase + cellOffset;
                Value[cell] = uint2(0u, 0u);
                ValueIndirect[cell] = uint2(0u, 0u);
                UpdateCellValue[cell * 4u + 0u] = 0u;
                UpdateCellValue[cell * 4u + 1u] = 0u;
                UpdateCellValue[cell * 4u + 2u] = 0u;
                UpdateCellValue[cell * 4u + 3u] = 0u;
                UpdateCellValueIndirect[cell * 4u + 0u] = 0u;
                UpdateCellValueIndirect[cell * 4u + 1u] = 0u;
                UpdateCellValueIndirect[cell * 4u + 2u] = 0u;
                UpdateCellValueIndirect[cell * 4u + 3u] = 0u;
            }
            TileUpdate[idx] = 0u;
        }
    }

    if (idx == 0u)
        TileUpdateCount[0] = 0;

    uint probeCount = ScreenProbes.ProbeCount.x * ScreenProbes.ProbeCount.y;
    uint shCount = probeCount * 9u;
    if (idx < shCount) {
        const uint probeIndex = idx / 9u;
        const uint lastUpdated = ProbeLastUpdated[probeIndex];
        const bool expired = ProbePosition[probeIndex].w > 0.0f &&
            GI12Params.FrameIndex > lastUpdated &&
            GI12Params.FrameIndex - lastUpdated > GI12_PROBE_DECAY_FRAMES;
        if (expired) {
            ProbeSH[idx] = 0u;
        } else {
            uint4 packed = ProbeSH[idx];
            int3 coefficient = asint(packed.xyz);
            coefficient = (int3)(float3(coefficient) * GI12_DECAY);
            packed.xyz = asuint(coefficient);
            packed.w = (uint)(float(packed.w) * GI12_DECAY);
            ProbeSH[idx] = packed;
        }
    }

    if (idx < probeCount) {
        const uint lastUpdated = ProbeLastUpdated[idx];
        if (ProbePosition[idx].w > 0.0f && GI12Params.FrameIndex > lastUpdated &&
            GI12Params.FrameIndex - lastUpdated > GI12_PROBE_DECAY_FRAMES) {
            ProbePosition[idx] = 0.0f;
            ProbeNormal[idx] = 0.0f;
        }
    }

    uint probeSize = clamp(ScreenProbes.ProbeSize, 4u, 16u);
    uint radianceCount = probeCount * probeSize * probeSize;
    if (idx < radianceCount) {
        float4 radiance = ProbeRadiance[idx];
        radiance.rgb *= GI12_DECAY;
        ProbeRadiance[idx] = radiance;
    }
}
