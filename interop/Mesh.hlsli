#ifndef SHAPE_HLSL
#define SHAPE_HLSL

#include "Interop.h"
#include "interop/VertexDesc.hlsli"
#include "interop/Properties.hlsli"

INTEROP_DATA_STRUCT(Mesh, 4)
{ 
    uint16_t IndexID;
    uint16_t VertexID;
    VertexDesc VertexDesc;
    uint MaterialOffset;
    uint NumTriangles;
    INTEROP_DATA_TYPE(Properties) Properties;
    INTEROP_ROW_MAJOR(float3x4) Transform;  
    INTEROP_ROW_MAJOR(float3x4) PrevTransform;
};
VALIDATE_ALIGNMENT(MeshData, 4);

#ifndef __cplusplus
namespace MeshFlags
{
    static const uint Dynamic = (1 << 1);
    static const uint Skinned = (1 << 2);
    static const uint DoubleSidedGeom = (1 << 5);
}

namespace DirtyFlags
{
    static const uint Skin = (1 << 1);
    static const uint Vertex = (1 << 2);
}
#endif

#endif