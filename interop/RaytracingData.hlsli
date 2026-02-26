#ifndef RAYTRACINGDATA_HLSL
#define RAYTRACINGDATA_HLSL

#include "Interop.h"
#include "Interop/Light.hlsli"

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
    uint Pad0;
    float3 EmittanceColor;
    uint Pad1; 
    INTEROP_DATA_TYPE(Light) DirectionalLight;    
};
VALIDATE_CBUFFER(RaytracingData, 16);

#endif