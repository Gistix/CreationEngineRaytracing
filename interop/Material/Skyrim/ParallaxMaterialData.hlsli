#ifndef PARALLAX_MATERIAL_DATA_HLSL
#define PARALLAX_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(ParallaxMaterialDataExtra, 4)
{
    uint16_t HeightTexture;
    half Pad;
};
VALIDATE_ALIGNMENT(ParallaxMaterialDataExtra, 4);

INTEROP_STRUCT(ParallaxMaterialData : LightingMaterialData, 4)
{
    uint16_t HeightTexture;
    half Pad;
};
VALIDATE_ALIGNMENT(ParallaxMaterialData, 4);
VALIDATE_SIZE(ParallaxMaterialData, sizeof(LightingMaterialData) + sizeof(ParallaxMaterialDataExtra));

#endif // PARALLAX_MATERIAL_DATA_HLSL
