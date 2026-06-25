#ifndef EFFECT_MATERIAL_DATA_HLSL
#define EFFECT_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(EffectMaterialDataExtra, 4)
{
    half4 BaseColor;
    half BaseColorScale;
    uint16_t EffectTexture;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(EffectMaterialDataExtra, 4);

INTEROP_STRUCT(EffectMaterialData : LightingMaterialData, 4)
{
    half4 BaseColor;
    half BaseColorScale;
    uint16_t EffectTexture;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(EffectMaterialData, 4);
VALIDATE_SIZE(EffectMaterialData, sizeof(LightingMaterialData) + sizeof(EffectMaterialDataExtra));

#endif // EFFECT_MATERIAL_DATA_HLSL
