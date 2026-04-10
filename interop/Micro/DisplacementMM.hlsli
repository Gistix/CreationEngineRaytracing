#pragma once

struct DisplacementMMData
{
    uint TriangleCount;
    uint SubdivisionLevel; // e.g. 4-6
    uint MicroVertexCount; // (N+1)*(N+2)/2
    uint PackedStride;     // bytes per triangle
    uint MeshIndex;
    uint DisplacementIndex;
    uint2 Pad;
};