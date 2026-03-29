#ifndef RESTIRGIDATA_HLSLI
#define RESTIRGIDATA_HLSLI

#include "Rtxdi/GI/ReSTIRGIParameters.h"
#include "Rtxdi/RtxdiParameters.h"

// Wrapper struct for the ReSTIR GI constant buffer.
// Uses native RTXDI parameter types directly.
struct ReSTIRGIData
{
    RTXDI_GIParameters giParams;
    RTXDI_RuntimeParameters runtimeParams;
};

#endif // RESTIRGIDATA_HLSLI
