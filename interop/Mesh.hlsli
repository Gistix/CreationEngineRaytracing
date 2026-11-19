#ifndef SHAPE_HLSL
#define SHAPE_HLSL

#include "Interop.h"
#include "interop/VertexDesc.hlsli"

#ifndef __cplusplus
namespace MeshType
{
    static const uint Base = 0;
    static const uint Default = 1;
    static const uint Skinned = 2;
    static const uint Dynamic = 3;
    static const uint SubIndex = 4;
    static const uint Instanced = 5;
    static const uint MergedInstance = 6;
    static const uint TreeAnim = 7;
}
#endif

namespace MeshDataFlags
{
    // Merged-instance grass with wind: per-instance swayed positions live in the dynamic position buffer
    // (DynamicID), current block [0, N*V) then previous block [N*V, 2*N*V), indexed by instance * V + vertex.
    static const uint32_t WindPositions = (1 << 0);
}

INTEROP_DATA_STRUCT(Mesh, 4)
{ 
    uint16_t IndexID;
    uint16_t VertexID;
    VertexDesc VertexDesc;
    uint16_t NumVertices;
    uint16_t NumTriangles;
    uint16_t Type;
    uint16_t DynamicID;
    uint16_t MeshID;
    uint16_t Flags;
    uint IndexOffset;
    uint VertexOffset;
    uint MaterialOffset;
    uint InstanceTransformBase;
    uint InstanceCount;
    uint Pad2;

    uint GetIndexOffset()
    {
        return IndexOffset;
    }

    uint GetVertexOffset()
    {
        return VertexOffset;
    }

    uint GetMaterialOffset()
    {
        return MaterialOffset;
    }

#ifndef __cplusplus
    // Meshes whose vertices are rewritten per frame and record previous positions for motion vectors.
    bool HasPrevPositions()
    {
        return Type == MeshType::Skinned || Type == MeshType::Dynamic || Type == MeshType::TreeAnim;
    }

    bool HasWindPositions()
    {
        return (Flags & MeshDataFlags::WindPositions) != 0;
    }
#endif
};
VALIDATE_TRIVIAL(MeshData);
VALIDATE_ALIGNMENT(MeshData, 4);
VALIDATE_SIZE(MeshData, 48);

#endif