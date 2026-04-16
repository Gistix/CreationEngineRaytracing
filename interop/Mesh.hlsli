#ifndef SHAPE_HLSL
#define SHAPE_HLSL

#include "Interop.h"
#include "interop/Material.hlsli"

INTEROP_DATA_STRUCT(Mesh, 4)
{
    INTEROP_DATA_TYPE(Material) Material;   
    uint GeometryIdx;
    uint Flags;
    uint Pad;
    INTEROP_ROW_MAJOR(float3x4) Transform;  
    INTEROP_ROW_MAJOR(float3x4) PrevTransform;
};
VALIDATE_ALIGNMENT(MeshData, 4);

#ifndef __cplusplus
namespace MeshDataFlags
{
    static const uint Dynamic = 1u << 1;
    static const uint Skinned = 1u << 2;
    static const uint DoubleSidedGeom = 1u << 5;
}
#endif

#endif