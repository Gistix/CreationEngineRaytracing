#ifndef LOD_LANDSCAPE_MATERIAL_DATA_HLSL
#define LOD_LANDSCAPE_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(LODLandscapeMaterialDataExtra, 4)
{
    uint16_t ParentDiffuseTexture;
    uint16_t ParentNormalTexture;
    uint16_t NoiseTexture;
    half TexOffsetX;
    half TexOffsetY;
    half TexFade;
};
VALIDATE_ALIGNMENT(LODLandscapeMaterialDataExtra, 4);

INTEROP_STRUCT(LODLandscapeMaterialData : LightingMaterialData, 4)
{
    uint16_t ParentDiffuseTexture;
    uint16_t ParentNormalTexture;
    uint16_t NoiseTexture;
    half TexOffsetX;
    half TexOffsetY;
    half TexFade;
};
VALIDATE_ALIGNMENT(LODLandscapeMaterialData, 4);
VALIDATE_SIZE(LODLandscapeMaterialData, sizeof(LightingMaterialData) + sizeof(LODLandscapeMaterialDataExtra));

#endif // LOD_LANDSCAPE_MATERIAL_DATA_HLSL
