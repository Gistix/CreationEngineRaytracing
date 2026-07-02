#ifndef HAIR_TINT_MATERIAL_DATA_HLSL
#define HAIR_TINT_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(HairTintMaterialDataExtra, 4)
{
    half3 TintColor;
    half Pad;
};
VALIDATE_ALIGNMENT(HairTintMaterialDataExtra, 4);

INTEROP_STRUCT(HairTintMaterialData : LightingMaterialData, 4)
{
    half3 TintColor;
    half Pad;
};
VALIDATE_ALIGNMENT(HairTintMaterialData, 4);
VALIDATE_SIZE(HairTintMaterialData, sizeof(LightingMaterialData) + sizeof(HairTintMaterialDataExtra));

#endif // HAIR_TINT_MATERIAL_DATA_HLSL
