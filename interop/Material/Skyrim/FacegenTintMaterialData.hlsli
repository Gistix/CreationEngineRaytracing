#ifndef FACEGEN_TINT_MATERIAL_DATA_HLSL
#define FACEGEN_TINT_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(FacegenTintMaterialDataExtra, 4)
{
    half3 TintColor;
    half Pad;
};
VALIDATE_ALIGNMENT(FacegenTintMaterialDataExtra, 4);

INTEROP_STRUCT(FacegenTintMaterialData : LightingMaterialData, 4)
{
    half3 TintColor;
    half Pad;
};
VALIDATE_ALIGNMENT(FacegenTintMaterialData, 4);
VALIDATE_SIZE(FacegenTintMaterialData, sizeof(LightingMaterialData) + sizeof(FacegenTintMaterialDataExtra));

#endif // FACEGEN_TINT_MATERIAL_DATA_HLSL
