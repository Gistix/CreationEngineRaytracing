StructuredBuffer<uint>   CounterBuffer    : register(t0);
StructuredBuffer<uint>   BinOffsets       : register(t1);
StructuredBuffer<uint>   RayKeys          : register(t2);

RWStructuredBuffer<uint> SortedRayIndices : register(u0);
RWStructuredBuffer<uint> BinCounters      : register(u1);

[numthreads(256, 1, 1)]
void Main(uint tid : SV_DispatchThreadID)
{
    uint activeCount = CounterBuffer[0];
    if (tid >= activeCount)
        return;

    uint key = RayKeys[tid];
    uint baseOffset = BinOffsets[key];

    uint localIdx;
    InterlockedAdd(BinCounters[key], 1, localIdx);

    SortedRayIndices[baseOffset + localIdx] = tid;
}
