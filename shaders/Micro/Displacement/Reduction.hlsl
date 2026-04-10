#include "Micro/DisplcementCB.hlsli"

ConstantBuffer<DMM> DMM;

StructuredBuffer<float>  MicroValues;
RWStructuredBuffer<float2> BiasScale;

[numthreads(64,1,1)]
void CS_MinMax(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint triIndex = gid.x;
    if (triIndex >= DMM.TriangleCount) return;

    uint N = DMM.SubdivisionLevel;
    uint total = (N + 1) * (N + 2) / 2;

    groupshared float localMin[64];
    groupshared float localMax[64];

    float minVal = 1e30;
    float maxVal = -1e30;

    for (uint i = tid.x; i < total; i += 64)
    {
        float v = MicroValues[triIndex * total + i];
        minVal = min(minVal, v);
        maxVal = max(maxVal, v);
    }

    localMin[tid.x] = minVal;
    localMax[tid.x] = maxVal;

    GroupMemoryBarrierWithGroupSync();

    // reduction
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (tid.x < stride)
        {
            localMin[tid.x] = min(localMin[tid.x], localMin[tid.x + stride]);
            localMax[tid.x] = max(localMax[tid.x], localMax[tid.x + stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (tid.x == 0)
    {
        float minV = localMin[0];
        float maxV = localMax[0];

        float scale = max(maxV - minV, 1e-6);

        BiasScale[triIndex] = float2(minV, scale);
    }
}