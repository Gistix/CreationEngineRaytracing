#ifndef LANDSCAPE_MATERIAL_DATA_HLSL
#define LANDSCAPE_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

// Layer 0 reuses the inherited LightingMaterialData DiffuseTexture / NormalTexture.
// These are the additional landscape layers (1-5) plus the terrain overlay/noise.
INTEROP_STRUCT(LandscapeMaterialDataExtra, 4)
{
    uint16_t DiffuseTexture1;
    uint16_t DiffuseTexture2;
    uint16_t DiffuseTexture3;
    uint16_t DiffuseTexture4;
    uint16_t DiffuseTexture5;
    uint16_t NormalTexture1;
    uint16_t NormalTexture2;
    uint16_t NormalTexture3;
    uint16_t NormalTexture4;
    uint16_t NormalTexture5;
    uint16_t OverlayTexture;
    uint16_t NoiseTexture;
};
VALIDATE_ALIGNMENT(LandscapeMaterialDataExtra, 4);

INTEROP_STRUCT(LandscapeMaterialData : LightingMaterialData, 4)
{
    uint16_t DiffuseTexture1;
    uint16_t DiffuseTexture2;
    uint16_t DiffuseTexture3;
    uint16_t DiffuseTexture4;
    uint16_t DiffuseTexture5;
    uint16_t NormalTexture1;
    uint16_t NormalTexture2;
    uint16_t NormalTexture3;
    uint16_t NormalTexture4;
    uint16_t NormalTexture5;
    uint16_t OverlayTexture;
    uint16_t NoiseTexture;
};
VALIDATE_ALIGNMENT(LandscapeMaterialData, 4);
VALIDATE_SIZE(LandscapeMaterialData, sizeof(LightingMaterialData) + sizeof(LandscapeMaterialDataExtra));

#endif // LANDSCAPE_MATERIAL_DATA_HLSL
