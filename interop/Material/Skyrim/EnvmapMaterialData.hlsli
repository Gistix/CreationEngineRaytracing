#ifndef ENVMAP_MATERIAL_DATA_HLSL
#define ENVMAP_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(EnvmapMaterialDataExtra, 4)
{
    uint16_t EnvironmentTexture;
    uint16_t EnvironmentMaskTexture;
    half EnvironmentScale;
    half Pad;
};
VALIDATE_ALIGNMENT(EnvmapMaterialDataExtra, 4);

INTEROP_STRUCT(EnvmapMaterialData : LightingMaterialData, 4)
{
    uint16_t EnvironmentTexture;
    uint16_t EnvironmentMaskTexture;
    half EnvironmentScale;
    half Pad;
};
VALIDATE_ALIGNMENT(EnvmapMaterialData, 4);
VALIDATE_SIZE(EnvmapMaterialData, sizeof(LightingMaterialData) + sizeof(EnvmapMaterialDataExtra));

#endif // ENVMAP_MATERIAL_DATA_HLSL
