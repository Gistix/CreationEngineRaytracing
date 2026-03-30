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
};
VALIDATE_ALIGNMENT(MeshData, 4);

#ifdef __cplusplus
namespace MeshDataFlags
{
    static constexpr uint32_t Dynamic = 1u << 1;
    static constexpr uint32_t Skinned = 1u << 2;
    static constexpr uint32_t DoubleSidedGeom = 1u << 5;
}
#else
namespace MeshDataFlags
{
    static const uint Dynamic = 1u << 1;
    static const uint Skinned = 1u << 2;
    static const uint DoubleSidedGeom = 1u << 5;
}
#endif

#endif