RWStructuredBuffer<uint> BinHistogram : register(u0);
RWStructuredBuffer<uint> BinOffsets   : register(u1);

groupshared uint s_Scan[256];

[numthreads(256, 1, 1)]
void Main(uint tid : SV_GroupIndex)
{
    s_Scan[tid] = BinHistogram[tid];
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint offset = 1; offset < 256; offset <<= 1)
    {
        uint temp = 0;
        if (tid >= offset)
            temp = s_Scan[tid - offset];
        GroupMemoryBarrierWithGroupSync();
        s_Scan[tid] += temp;
        GroupMemoryBarrierWithGroupSync();
    }

    uint exclusiveSum = (tid == 0) ? 0 : s_Scan[tid - 1];
    BinOffsets[tid] = exclusiveSum;
}
