#include "Triangle.hlsli"

// One thread per base triangle
// Reads subdivision levels, writes edgeFlags into OutTriangleMeta

StructuredBuffer<Triangle>   BaseTriangles   : register(t0);
StructuredBuffer<uint>    SubdivLevels    : register(t1); // one per triangle
RWStructuredBuffer<uint2> OutTriangleMeta : register(u0);

// Adjacency map: for each directed edge (v0,v1) → triangle index
// Built on CPU, uploaded as a structured buffer
// Key = (min(v0,v1) << 16 | max(v0,v1)), Value = neighbor triIdx
StructuredBuffer<uint>    AdjacencyKeys   : register(t2);
StructuredBuffer<uint>    AdjacencyVals   : register(t3);
uint                      AdjacencyCount  : register(b0);

uint FindNeighbor(uint va, uint vb)
{
    uint key = (min(va, vb) << 16) | max(va, vb);
    // Linear scan — replace with binary search for large meshes
    for (uint i = 0; i < AdjacencyCount; ++i)
        if (AdjacencyKeys[i] == key) return AdjacencyVals[i];
    return 0xFFFFFFFF; // boundary edge
}

[numthreads(64, 1, 1)]
void CSEdgeFlags(uint3 id : SV_DispatchThreadID)
{
    uint triIdx = id.x;
    if (triIdx >= AdjacencyCount) return;

    Triangle tri     = BaseTriangles[triIdx];
    uint     myLevel = SubdivLevels[triIdx];
    uint     flags   = 0;

    uint edges[3][2] = { {tri.x, tri.y}, {tri.y, tri.z}, {tri.z, tri.x} };
    for (uint e = 0; e < 3; ++e)
    {
        uint neighbor = FindNeighbor(edges[e][0], edges[e][1]);
        if (neighbor != 0xFFFFFFFF && SubdivLevels[neighbor] < myLevel)
            flags |= (1u << e);
    }

    // Patch flags into existing metadata
    uint2 meta = OutTriangleMeta[triIdx];
    meta.y = (meta.y & 0xFF) | (flags << 8);
    OutTriangleMeta[triIdx] = meta;
}