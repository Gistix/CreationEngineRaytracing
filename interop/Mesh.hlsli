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
    uint16_t NumVertices;
    uint16_t NumTriangles;
    INTEROP_DATA_TYPE(Properties) Properties;
    uint16_t Type;
    uint16_t DynamicID;
    uint MaterialOffsetComp;
    INTEROP_ROW_MAJOR(float3x4) Transform;  
    INTEROP_ROW_MAJOR(float3x4) PrevTransform;
    
    uint GetMaterialOffset()
    {
        return MaterialOffsetComp * 4;
    }
};
VALIDATE_ALIGNMENT(MeshData, 4);

#ifndef __cplusplus
namespace MeshType
{
    static const uint Base = 0;
    static const uint Default = 1;
    static const uint Skinned = 2;
    static const uint Dynamic = 3;
}

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