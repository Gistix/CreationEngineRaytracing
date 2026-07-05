#ifndef WATER_MATERIAL_DATA_HLSL
#define WATER_MATERIAL_DATA_HLSL

#include "interop/Interop.h"
#include "interop/Material/MaterialBaseData.hlsli"

// WaterMaterialData inherits directly from MaterialBaseData (not LightingMaterialData).
// Water materials are fully emissive/transmissive with animated normals.

INTEROP_STRUCT(WaterMaterialData : MaterialBaseData, 4)
{
    half4 NormalScrolls;         // (scroll1.xy, scroll2.xy)
    half4 NormalScroll3AndScale; // (scroll3.xy, uvScale[0], uvScale[1])
    half4 UVScaleAndObjectUV;    // (uvScale[2], objectUV.xyz)
    half4 CellTexCoordOffset;    // (flowX, flowY, cellX, cellY)
    half Amplitude0;
    half Amplitude1;
    half Amplitude2;
    half4 ShallowColor;          // shallowWaterColor (RGB)
    uint16_t NormalsTexture0;
    uint16_t NormalsTexture1;
    uint16_t NormalsTexture2;
    uint16_t FlowmapTexture;
    uint16_t Pad;
};
VALIDATE_ALIGNMENT(WaterMaterialData, 4);

#endif // WATER_MATERIAL_DATA_HLSL
