#ifndef PBR_MATERIAL_DATA_HLSL
#define PBR_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

// Base color / normal reuse the inherited LightingMaterialData DiffuseTexture / NormalTexture.
// FeatureColor / FeatureScalar / FeaturesTexture0-1 are shared across the mutually-exclusive
// PBR features (subsurface / coat (two-layer) / fuzz), selected by PBRFlags.
INTEROP_STRUCT(PBRMaterialDataExtra, 4)
{
    half4 FeatureColor;
    half4 GlintParameters;
    uint16_t PBRFlags;
    uint16_t RMAOSTexture;
    uint16_t EmissiveTexture;
    uint16_t FeaturesTexture0;
    uint16_t FeaturesTexture1;
    half RoughnessScale;
    half SpecularLevel;
    half FeatureScalar;
};
VALIDATE_ALIGNMENT(PBRMaterialDataExtra, 4);

INTEROP_STRUCT(PBRMaterialData : LightingMaterialData, 4)
{
    half4 FeatureColor;
    half4 GlintParameters;
    uint16_t PBRFlags;
    uint16_t RMAOSTexture;
    uint16_t EmissiveTexture;
    uint16_t FeaturesTexture0;
    uint16_t FeaturesTexture1;
    half RoughnessScale;
    half SpecularLevel;
    half FeatureScalar;
};
VALIDATE_ALIGNMENT(PBRMaterialData, 4);
VALIDATE_SIZE(PBRMaterialData, sizeof(LightingMaterialData) + sizeof(PBRMaterialDataExtra));

#endif // PBR_MATERIAL_DATA_HLSL
