#ifndef PBR_LANDSCAPE_MATERIAL_DATA_HLSL
#define PBR_LANDSCAPE_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/Skyrim/LightingMaterialData.hlsli"

// PBR landscape carries all six layers explicitly (base color / normal / RMAOS) plus overlay/noise
// and the per-layer roughness / displacement / specular scales.
INTEROP_STRUCT(PBRLandscapeMaterialDataExtra, 4)
{
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
VALIDATE_ALIGNMENT(PBRLandscapeMaterialDataExtra, 4);

INTEROP_STRUCT(PBRLandscapeMaterialData : LightingMaterialData, 4)
{
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
VALIDATE_SIZE(PBRLandscapeMaterialData, sizeof(LightingMaterialData) + sizeof(PBRLandscapeMaterialDataExtra));

#endif // PBR_LANDSCAPE_MATERIAL_DATA_HLSL
