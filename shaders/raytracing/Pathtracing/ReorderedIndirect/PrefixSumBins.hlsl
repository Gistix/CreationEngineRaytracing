RWStructuredBuffer<uint> BinHistogram : register(u0);
RWStructuredBuffer<uint> BinOffsets   : register(u1);

groupshared uint s_Scan[1024];

[numthreads(1024, 1, 1)]
void Main(uint tid : SV_GroupIndex)
{
    uint baseBin = tid * 4u;

    // 1. Load 4 bins per thread
    uint v0 = BinHistogram[baseBin + 0u];
    uint v1 = BinHistogram[baseBin + 1u];
    uint v2 = BinHistogram[baseBin + 2u];
    uint v3 = BinHistogram[baseBin + 3u];

    // 2. Local prefix sums within the thread's 4 elements
    uint sum0 = v0;
    uint sum1 = sum0 + v1;
    uint sum2 = sum1 + v2;
    uint threadTotal = sum2 + v3;

    // 3. Store total into shared memory for cross-thread prefix sum
    s_Scan[tid] = threadTotal;
    GroupMemoryBarrierWithGroupSync();

    // 4. Parallel prefix sum across 1024 threads
    [unroll]
    for (uint offset = 1; offset < 1024; offset <<= 1)
    {
        uint temp = 0;
        if (tid >= offset)
            temp = s_Scan[tid - offset];
        GroupMemoryBarrierWithGroupSync();
        s_Scan[tid] += temp;
        GroupMemoryBarrierWithGroupSync();
    }

    // 5. Exclusive base prefix for this thread
    uint threadBase = (tid == 0) ? 0 : s_Scan[tid - 1];

    // 6. Write exclusive offsets for all 4 bins
    BinOffsets[baseBin + 0u] = threadBase;
    BinOffsets[baseBin + 1u] = threadBase + sum0;
    BinOffsets[baseBin + 2u] = threadBase + sum1;
    BinOffsets[baseBin + 3u] = threadBase + sum2;
}
