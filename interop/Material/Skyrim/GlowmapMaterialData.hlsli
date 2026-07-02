#ifndef GLOWMAP_MATERIAL_DATA_HLSL
#define GLOWMAP_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(GlowmapMaterialDataExtra, 4)
{
    uint16_t GlowTexture;
    half Pad;
};
VALIDATE_ALIGNMENT(GlowmapMaterialDataExtra, 4);

INTEROP_STRUCT(GlowmapMaterialData : LightingMaterialData, 4)
{
    uint16_t GlowTexture;
    half Pad;
};
VALIDATE_ALIGNMENT(GlowmapMaterialData, 4);
VALIDATE_SIZE(GlowmapMaterialData, sizeof(LightingMaterialData) + sizeof(GlowmapMaterialDataExtra));

#endif // GLOWMAP_MATERIAL_DATA_HLSL
