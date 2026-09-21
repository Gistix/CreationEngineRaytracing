#include "interop/RayRecord.hlsli"
#include "raytracing/Pathtracing/ReorderedIndirect/WaveAggregation.hlsli"

StructuredBuffer<uint>      CounterBuffer    : register(t0);
StructuredBuffer<uint>      BinOffsets       : register(t1);
StructuredBuffer<uint>      RayKeys          : register(t2);
StructuredBuffer<RayRecord> RayRecords       : register(t3);

RWStructuredBuffer<RayRecord> SortedRayRecords : register(u0);
RWStructuredBuffer<uint>      BinCounters      : register(u1);

[numthreads(256, 1, 1)]
void Main(uint tid : SV_DispatchThreadID)
{
    uint activeCount = CounterBuffer[0];
    if (tid >= activeCount)
        return;

    uint key = RayKeys[tid];
    uint baseOffset = BinOffsets[key];

    // Read record sequentially from unordered buffer
    RayRecord record = RayRecords[tid];

    // Wave-level aggregation for BinCounters[key]
    WaveKeyMatch match = WaveMatchKey(key);
    uint waveKeyBase = 0;
    if (match.isLeader)
    {
        InterlockedAdd(BinCounters[key], match.matchCount, waveKeyBase);
    }
    waveKeyBase = WaveReadLaneAt(waveKeyBase, match.leaderLane);

    uint localIdx = waveKeyBase + match.prefixIndex;
    SortedRayRecords[baseOffset + localIdx] = record;
}
