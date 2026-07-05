#ifndef PARALLAX_OCC_MATERIAL_DATA_HLSL
#define PARALLAX_OCC_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(ParallaxOccMaterialDataExtra, 4)
{
    uint16_t HeightTexture;
    half MaxPasses;
    half Scale;
    half Pad;
};
VALIDATE_ALIGNMENT(ParallaxOccMaterialDataExtra, 4);

INTEROP_STRUCT(ParallaxOccMaterialData : LightingMaterialData, 4)
{
    uint16_t HeightTexture;
    half MaxPasses;
    half Scale;
    half Pad;
};
VALIDATE_ALIGNMENT(ParallaxOccMaterialData, 4);
VALIDATE_SIZE(ParallaxOccMaterialData, sizeof(LightingMaterialData) + sizeof(ParallaxOccMaterialDataExtra));

#endif // PARALLAX_OCC_MATERIAL_DATA_HLSL
