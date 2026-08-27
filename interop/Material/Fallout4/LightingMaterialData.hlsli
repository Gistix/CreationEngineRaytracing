#ifndef LIGHTING_MATERIAL_HLSL
#define LIGHTING_MATERIAL_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"

INTEROP_STRUCT(LightingMaterialData : MaterialBaseData, 4)
{
    half3 SpecularColor;
    half MaterialAlpha;
    
    half RefractionPower;
    half Smoothness;
    half SpecularColorScale;
    half FresnelPower;

    half WetnessControl_SpecScale;
    half WetnessControl_SpecPowerScale;
    half WetnessControl_SpecMin;
    half WetnessControl_EnvMapScale;

    half WetnessControl_FresnelPower;
    half WetnessControl_Metalness;
    half SubSurfaceLightRolloff;
    half RimLightPower;

    half BackLightPower;
    half LookupScale;
    uint16_t DiffuseTexture;
    uint16_t NormalTexture;

    uint16_t RimSoftLightingTexture;
    uint16_t SmoothnessSpecMaskTexture;
    uint16_t LookupTexture;
    
    // PBR field
    half MetallicScale;
};
VALIDATE_ALIGNMENT(LightingMaterialData, 4);

#endif // LIGHTING_MATERIAL_HLSL

