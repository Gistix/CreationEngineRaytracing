#ifndef PBR_MATERIAL_DATA_HLSL
#define PBR_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

// PBRMaterialData inherits directly from MaterialBaseData (not LightingMaterialData).
// Fields that overlap with LightingMaterialData are placed at the same offsets
// (matching BSLightingShaderMaterialPBR's inheritance from BSLightingShaderMaterialBase).
// Names are chosen to reflect PBR semantics.
//
// FeatureColor / FeatureScalar / FeaturesTexture0-1 are shared across the mutually-exclusive
// PBR features (subsurface / coat (two-layer) / fuzz), selected by PBRFlags.

INTEROP_STRUCT(PBRMaterialData : MaterialBaseData, 4)
{
    // --- LightingMaterialData-equivalent fields (same offsets as BSLightingShaderMaterialBase) ---
    half3 SpecularColor;
    half SpecularLevel;
    half RoughnessScale;

    uint16_t DiffuseTexture;
    uint16_t NormalTexture;
    uint16_t RimSoftLightingTexture;
    uint16_t SpecularBackLightingTexture;
    half MaterialAlpha;

    // --- PBR-specific fields ---
    half4 FeatureColor;
    half4 GlintParameters;
    uint16_t PBRFlags;
    uint16_t RMAOSTexture;
    uint16_t EmissiveTexture;
    uint16_t DisplacementTexture;
    uint16_t FeaturesTexture0;
    uint16_t FeaturesTexture1;
    half FeatureScalar;
    half DisplacementScale;
};
VALIDATE_ALIGNMENT(PBRMaterialData, 4);

// Verify that PBRMaterialData fields align with LightingMaterialData at the same offsets
VALIDATE_OFFSET(PBRMaterialData, SpecularColor, offsetof(LightingMaterialData, SpecularColor));
VALIDATE_OFFSET(PBRMaterialData, SpecularLevel, offsetof(LightingMaterialData, SpecularPower));
VALIDATE_OFFSET(PBRMaterialData, RoughnessScale, offsetof(LightingMaterialData, SpecularColorScale));
VALIDATE_OFFSET(PBRMaterialData, DiffuseTexture, offsetof(LightingMaterialData, DiffuseTexture));
VALIDATE_OFFSET(PBRMaterialData, NormalTexture, offsetof(LightingMaterialData, NormalTexture));
VALIDATE_OFFSET(PBRMaterialData, RimSoftLightingTexture, offsetof(LightingMaterialData, RimSoftLightingTexture));
VALIDATE_OFFSET(PBRMaterialData, SpecularBackLightingTexture, offsetof(LightingMaterialData, SpecularBackLightingTexture));
VALIDATE_OFFSET(PBRMaterialData, MaterialAlpha, offsetof(LightingMaterialData, MaterialAlpha));

#endif // PBR_MATERIAL_DATA_HLSL
