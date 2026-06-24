#ifndef PBR_MATERIAL_DATA_HLSL
#define PBR_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

// Base color / normal reuse the inherited LightingMaterialData DiffuseTexture / NormalTexture.
INTEROP_STRUCT(PBRMaterialDataExtra, 4)
{
    uint16_t PBRFlags;
    uint16_t RMAOSTexture;
    uint16_t EmissiveTexture;
    half RoughnessScale;
    half SpecularLevel;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(PBRMaterialDataExtra, 4);

INTEROP_STRUCT(PBRMaterialData : LightingMaterialData, 4)
{
    uint16_t PBRFlags;
    uint16_t RMAOSTexture;
    uint16_t EmissiveTexture;
    half RoughnessScale;
    half SpecularLevel;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(PBRMaterialData, 4);
VALIDATE_SIZE(PBRMaterialData, sizeof(LightingMaterialData) + sizeof(PBRMaterialDataExtra));

#endif // PBR_MATERIAL_DATA_HLSL
