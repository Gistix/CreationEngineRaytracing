#ifndef EFFECT_MATERIAL_DATA_HLSL
#define EFFECT_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"

// EffectMaterialData inherits directly from MaterialBaseData (not LightingMaterialData).
// Effect materials (fire, smoke, glow) are emissive-only; they have no diffuse/normal/specular.

INTEROP_STRUCT(EffectMaterialData : MaterialBaseData, 4)
{
    half4 BaseColor;           // BSEffectShaderMaterial::baseColor (RGBA)
    half BaseColorScale;       // BSEffectShaderMaterial::baseColorScale
    uint16_t SourceTexture;    // BSEffectShaderMaterial::sourceTexture (main sprite)
    uint16_t EffectTexture;    // BSEffectShaderMaterial::greyscaleTexture (palette lookup)
};
VALIDATE_ALIGNMENT(EffectMaterialData, 4);

#endif // EFFECT_MATERIAL_DATA_HLSL
