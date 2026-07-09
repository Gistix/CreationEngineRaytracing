#ifndef WATER_MATERIAL_DATA_HLSL
#define WATER_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"

// WaterMaterialData inherits directly from MaterialBaseData (not LightingMaterialData).
// Water materials are fully emissive/transmissive with animated normals.

INTEROP_STRUCT(WaterMaterialData : MaterialBaseData, 4)
{
    half2 NormalScroll1;
    half2 NormalScroll2;
    half2 NormalScroll3;
    half UVScale1;
    half UVScale2;
    half UVScale3;
    half Amplitude1;
    half Amplitude2;
    half Amplitude3;  
    half3 ShallowColor;
    half Amplitude4;
    uint16_t NormalsTexture1;
    uint16_t NormalsTexture2;
    uint16_t NormalsTexture3;
    uint16_t NormalsTexture4;
};
VALIDATE_ALIGNMENT(WaterMaterialData, 4);

#endif // WATER_MATERIAL_DATA_HLSL
