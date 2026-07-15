#ifndef PBR_LANDSCAPE_MATERIAL_DATA_HLSL
#define PBR_LANDSCAPE_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

// PBRLandscapeMaterialData inherits directly from MaterialBaseData (not LightingMaterialData).
// Fields that overlap with LightingMaterialData are placed at the same offsets
// (matching BSLightingShaderMaterialPBRLandscape's inheritance from BSLightingShaderMaterialBase).

INTEROP_STRUCT(PBRLandscapeMaterialData : MaterialBaseData, 4)
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

    // --- PBR landscape-specific fields (six layers + overlay/noise + per-layer scales) ---
    uint16_t PBRFlags;
    uint16_t BaseColorTexture0;
    uint16_t BaseColorTexture1;
    uint16_t BaseColorTexture2;
    uint16_t BaseColorTexture3;
    uint16_t BaseColorTexture4;
    uint16_t BaseColorTexture5;
    uint16_t NormalTexture0;
    uint16_t NormalTexture1;
    uint16_t NormalTexture2;
    uint16_t NormalTexture3;
    uint16_t NormalTexture4;
    uint16_t NormalTexture5;
    uint16_t DisplacementTexture0;
    uint16_t DisplacementTexture1;
    uint16_t DisplacementTexture2;
    uint16_t DisplacementTexture3;
    uint16_t DisplacementTexture4;
    uint16_t DisplacementTexture5;
    uint16_t RMAOSTexture0;
    uint16_t RMAOSTexture1;
    uint16_t RMAOSTexture2;
    uint16_t RMAOSTexture3;
    uint16_t RMAOSTexture4;
    uint16_t RMAOSTexture5;
    uint16_t OverlayTexture;
    uint16_t NoiseTexture;
    half RoughnessScale0;
    half RoughnessScale1;
    half RoughnessScale2;
    half RoughnessScale3;
    half RoughnessScale4;
    half RoughnessScale5;
    half DisplacementScale0;
    half DisplacementScale1;
    half DisplacementScale2;
    half DisplacementScale3;
    half DisplacementScale4;
    half DisplacementScale5;
    half SpecularLevel0;
    half SpecularLevel1;
    half SpecularLevel2;
    half SpecularLevel3;
    half SpecularLevel4;
    half SpecularLevel5;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(PBRLandscapeMaterialData, 4);

// Verify that PBRLandscapeMaterialData fields align with LightingMaterialData at the same offsets
VALIDATE_OFFSET(PBRLandscapeMaterialData, SpecularColor, offsetof(LightingMaterialData, SpecularColor));
VALIDATE_OFFSET(PBRLandscapeMaterialData, SpecularLevel, offsetof(LightingMaterialData, SpecularPower));
VALIDATE_OFFSET(PBRLandscapeMaterialData, RoughnessScale, offsetof(LightingMaterialData, SpecularColorScale));
VALIDATE_OFFSET(PBRLandscapeMaterialData, DiffuseTexture, offsetof(LightingMaterialData, DiffuseTexture));
VALIDATE_OFFSET(PBRLandscapeMaterialData, NormalTexture, offsetof(LightingMaterialData, NormalTexture));
VALIDATE_OFFSET(PBRLandscapeMaterialData, RimSoftLightingTexture, offsetof(LightingMaterialData, RimSoftLightingTexture));
VALIDATE_OFFSET(PBRLandscapeMaterialData, SpecularBackLightingTexture, offsetof(LightingMaterialData, SpecularBackLightingTexture));
VALIDATE_OFFSET(PBRLandscapeMaterialData, MaterialAlpha, offsetof(LightingMaterialData, MaterialAlpha));

#endif // PBR_LANDSCAPE_MATERIAL_DATA_HLSL
