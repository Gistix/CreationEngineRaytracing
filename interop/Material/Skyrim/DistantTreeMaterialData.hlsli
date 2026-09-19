#ifndef DISTANT_TREE_MATERIAL_DATA_HLSL
#define DISTANT_TREE_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"

INTEROP_STRUCT(DistantTreeMaterialData : MaterialBaseData, 4)
{
    uint16_t TreeLODAtlas;
    uint16_t TreeLODAtlasNormal;
};
VALIDATE_ALIGNMENT(DistantTreeMaterialData, 4);

#endif // DISTANT_TREE_MATERIAL_DATA_HLSL
