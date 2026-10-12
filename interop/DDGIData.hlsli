#ifndef DDGIDATA_HLSLI
#define DDGIDATA_HLSLI

#ifndef HLSL
#include "Interop.h"

INTEROP_STRUCT(DDGIVolumeDescGPUPacked, 16)
{
    float3   origin;
    float    probeHysteresis;
    float4   rotation;
    float4   probeRayRotation;
    float    probeMaxRayDistance;
    float    probeNormalBias;
    float    probeViewBias;
    float    probeDistanceExponent;
    float    probeIrradianceEncodingGamma;
    float    probeIrradianceThreshold;
    float    probeBrightnessThreshold;
    float    probeMinFrontfaceDistance;
    float3   probeSpacing;
    uint     packed0;
    uint     packed1;
    uint     packed2;
    uint     packed3;
    uint     packed4;
    uint4    reserved;
};
VALIDATE_SIZE(DDGIVolumeDescGPUPacked, 128);
VALIDATE_ALIGNMENT(DDGIVolumeDescGPUPacked, 16);

#else
#include "interop/Interop.h"
#include "ddgi/include/Common.hlsl"
#endif

INTEROP_STRUCT(DDGIData, 16)
{
    DDGIVolumeDescGPUPacked Volume;
    uint DebugView;
};
#ifndef HLSL
VALIDATE_CBUFFER(DDGIData, 16);
#endif

#endif // DDGIDATA_HLSLI
