#ifndef FACEGEN_MATERIAL_DATA_HLSL
#define FACEGEN_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

INTEROP_STRUCT(FacegenMaterialDataExtra, 4)
{
    uint16_t TintTexture;
    uint16_t DetailTexture;
    uint16_t SubsurfaceTexture;
    half Pad;
};
VALIDATE_ALIGNMENT(FacegenMaterialDataExtra, 4);

INTEROP_STRUCT(FacegenMaterialData : LightingMaterialData, 4)
{
    uint16_t TintTexture;
    uint16_t DetailTexture;
    uint16_t SubsurfaceTexture;
    half Pad;
};
VALIDATE_ALIGNMENT(FacegenMaterialData, 4);
VALIDATE_SIZE(FacegenMaterialData, sizeof(LightingMaterialData) + sizeof(FacegenMaterialDataExtra));

#endif // FACEGEN_MATERIAL_DATA_HLSL
