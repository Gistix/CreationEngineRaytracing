#ifndef RESTIR_PT_DATA_HLSLI
#define RESTIR_PT_DATA_HLSLI

#include "Rtxdi/PT/ReSTIRPTParameters.h"
#include "Rtxdi/RtxdiParameters.h"

// Constant buffer structure for ReSTIR PT passes.
// Uses native RTXDI parameter types directly.
struct ReSTIRPTData
{
    RTXDI_PTParameters ptParams;
    RTXDI_RuntimeParameters runtimeParams;
};

#endif // RESTIR_PT_DATA_HLSLI
