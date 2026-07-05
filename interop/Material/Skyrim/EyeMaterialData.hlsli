#ifndef EYE_MATERIAL_DATA_HLSL
#define EYE_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(EyeMaterialDataExtra, 4)
{
    uint16_t EnvironmentTexture;
    uint16_t EnvironmentMaskTexture;
    half EnvironmentScale;
    half Pad;
};
VALIDATE_ALIGNMENT(EyeMaterialDataExtra, 4);

INTEROP_STRUCT(EyeMaterialData : LightingMaterialData, 4)
{
    uint16_t EnvironmentTexture;
    uint16_t EnvironmentMaskTexture;
    half EnvironmentScale;
    half Pad;
};
VALIDATE_ALIGNMENT(EyeMaterialData, 4);
VALIDATE_SIZE(EyeMaterialData, sizeof(LightingMaterialData) + sizeof(EyeMaterialDataExtra));

#endif // EYE_MATERIAL_DATA_HLSL
