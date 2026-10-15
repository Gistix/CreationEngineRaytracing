/**********************************************************************
Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
MIT License
********************************************************************/

#include "GI12Registers.hlsli"
#include "GI12Common.hlsli"

RWStructuredBuffer<uint4>  g_Reservoir_IndirectSampleReservoirBuffer : register(u0);
RWStructuredBuffer<uint4>  g_Reservoir_MultibounceReservoirBuffer    : register(u1);
RWStructuredBuffer<uint>   g_HashGridCache_VisibilityCountBuffer0    : register(u2);
RWStructuredBuffer<uint>   g_HashGridCache_VisibilityCountBuffer1    : register(u3);

[numthreads(128, 1, 1)]
void ClearReservoirs(uint did : SV_DispatchThreadID)
{
    if (did >= WorldSpaceReSTIRCB.num_cells * WorldSpaceReSTIRCB.num_entries_per_cell)
        return;

    g_Reservoir_IndirectSampleReservoirBuffer[did] = uint4(0, 0, 0, 0);
}

[numthreads(64, 1, 1)]
void GenerateReservoirs(uint did : SV_DispatchThreadID)
{
    uint count = g_HashGridCache_VisibilityCountBuffer0[0];
    if (did >= count)
        return;

    // Initialize reservoir for hit point
    Reservoir r = makeReservoir();
    float candidate_y = 1.0f; // placeholder radiance weight
    float weight = 1.0f;
    float rng = CalculateHaltonSequence(did + GI12CB.frame_index).x;

    updateReservoir(r, candidate_y, weight, rng);
    r.W = (r.M > 0.0f && r.w_sum > 0.0f) ? (r.w_sum / r.M) : 0.0f;

    g_Reservoir_IndirectSampleReservoirBuffer[did] = packReservoir(r);
}

[numthreads(64, 1, 1)]
void GenerateMultibounceReservoirs(uint did : SV_DispatchThreadID)
{
    uint count = g_HashGridCache_VisibilityCountBuffer1[0];
    if (did >= count)
        return;

    Reservoir r = makeReservoir();
    float candidate_y = 0.5f;
    float weight = 0.5f;
    float rng = CalculateHaltonSequence(did + GI12CB.frame_index * 2).x;

    updateReservoir(r, candidate_y, weight, rng);
    r.W = (r.M > 0.0f && r.w_sum > 0.0f) ? (r.w_sum / r.M) : 0.0f;

    g_Reservoir_IndirectSampleReservoirBuffer[count + did] = packReservoir(r);
}

[numthreads(64, 1, 1)]
void CompactReservoirs(uint did : SV_DispatchThreadID)
{
    uint count = g_HashGridCache_VisibilityCountBuffer0[0];
    if (did >= count)
        return;

    uint4 packedR = g_Reservoir_IndirectSampleReservoirBuffer[did];
    Reservoir r = unpackReservoir(packedR);
    r.M = min(r.M, 20.0f);
    r.W = (r.M > 0.0f && r.w_sum > 0.0f) ? (r.w_sum / r.M) : 0.0f;
    g_Reservoir_IndirectSampleReservoirBuffer[did] = packReservoir(r);
}

[numthreads(64, 1, 1)]
void ResampleReservoirs(uint did : SV_DispatchThreadID)
{
    uint count = g_HashGridCache_VisibilityCountBuffer0[0];
    if (did >= count)
        return;

    uint4 packedR = g_Reservoir_IndirectSampleReservoirBuffer[did];
    Reservoir r = unpackReservoir(packedR);

    // Spatial reuse with neighbors
    if (did > 0)
    {
        uint4 neighborPacked = g_Reservoir_IndirectSampleReservoirBuffer[did - 1];
        Reservoir neighborR = unpackReservoir(neighborPacked);
        float rng = CalculateHaltonSequence(did + GI12CB.frame_index * 3).y;
        updateReservoir(r, neighborR.y, neighborR.w_sum, rng);
    }

    r.W = (r.M > 0.0f && r.w_sum > 0.0f) ? (r.w_sum / r.M) : 0.0f;
    g_Reservoir_IndirectSampleReservoirBuffer[did] = packReservoir(r);
}
