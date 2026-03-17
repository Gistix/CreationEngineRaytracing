#ifndef RAYTRACINGDATA_HLSL
#define RAYTRACINGDATA_HLSL

#include "Interop.h"
#include "Interop/Light.hlsli"

INTEROP_STRUCT(SubSurfaceScattering, 16)
{
    uint SampleCount;
    float MaxSampleRadius;
    uint MaterialOverride;
    uint EnableTransmission;
    float3 TransmissionColorOverride;
    float ScaleOverride;
    float3 ScatteringColorOverride;
    float AnisotropyOverride; 
};
VALIDATE_CBUFFER(SubSurfaceScattering, 16);

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
    float Directional;
    uint3 Pad;
    float Point; 
    INTEROP_DATA_TYPE(Light) DirectionalLight;
    SubSurfaceScattering SubSurfaceScattering; 
};
VALIDATE_CBUFFER(RaytracingData, 16);

#endif