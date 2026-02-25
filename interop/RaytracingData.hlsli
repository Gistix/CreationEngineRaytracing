#ifndef RAYTRACINGDATA_HLSL
#define RAYTRACINGDATA_HLSL

#include "Interop.h"

INTEROP_STRUCT(RaytracingData, 16)
{
    float PixelConeSpreadAngle;
    float TexLODBias;
    uint NumLights;
    uint RussianRoulette;
    float2 Roughness;
    float2 Metalness;    
    float Emissive;
    float Effect;
    float Sky;
    float3 EmittanceColor;
    uint2 Pad;
};
VALIDATE_CBUFFER(RaytracingData, 16);

#endif