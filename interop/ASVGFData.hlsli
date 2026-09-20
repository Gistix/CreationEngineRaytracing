#ifndef ASVGFDATA_HLSL
#define ASVGFDATA_HLSL

#include "Interop.h"

INTEROP_STRUCT(ASVGFData, 16)
{
    float2 RenderSize;
    float2 InvRenderSize;

    int Iteration;
    int StepSize;
    float DepthSigma;
    float NormalSigma;

    float LuminanceSigma;
    float TemporalAlphaMin;
    float TemporalAlphaMax;
    float GradientSensitivity;

    int MaxHistoryLength;
    int DenoiseSpecular;
    float2 Pad;
};
VALIDATE_CBUFFER(ASVGFData, 16);

INTEROP_STRUCT(AtrousPushConstants, 4)
{
    int StepSize;
    int Iteration;
};

#endif
