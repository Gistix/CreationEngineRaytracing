#ifndef LIGHTING_MATERIAL_HLSL
#define LIGHTING_MATERIAL_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"

INTEROP_STRUCT(LightingMaterialData : MaterialBaseData, 4)
{
    half3 SpecularColor;
    half SpecularPower;
    
    uint16_t DiffuseTexture;
    uint16_t NormalTexture;
    uint16_t RimSoftLightingTexture;
    uint16_t SpecularBackLightingTexture;  
};
VALIDATE_ALIGNMENT(LightingMaterialData, 4);

#endif // LIGHTING_MATERIAL_HLSL