// ReSTIR GI Spatial Resampling Compute Shader
// Combines GI reservoirs from spatially neighboring pixels.

#include "RtxdiApplicationBridge.hlsli"
#include <Rtxdi/GI/SpatialResampling.hlsli>

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    if (any(GlobalIndex >= Camera.RenderSize))
        return;

    const RTXDI_GIParameters giParams = g_ReSTIRGI.giParams;
    const RTXDI_RuntimeParameters runtimeParams = g_ReSTIRGI.runtimeParams;

    RAB_Surface surface = RAB_GetGBufferSurface(GlobalIndex, false);

    if (!RAB_IsSurfaceValid(surface))
    {
        RTXDI_StoreGIReservoir(RTXDI_EmptyGIReservoir(),
            giParams.reservoirBufferParams,
            GlobalIndex,
            giParams.bufferIndices.spatialResamplingOutputBufferIndex);
        return;
    }

    // Load the input reservoir (output of temporal pass)
    RTXDI_GIReservoir inputReservoir = RTXDI_LoadGIReservoir(
        giParams.reservoirBufferParams,
        GlobalIndex,
        giParams.bufferIndices.spatialResamplingInputBufferIndex);

    // Initialize random sampler (use pass=1 for different sequence than temporal)
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(GlobalIndex, runtimeParams.frameIndex, 1);

    // Perform spatial resampling
    RTXDI_GIReservoir spatialResult = RTXDI_GISpatialResampling(
        GlobalIndex,
        surface,
        giParams.bufferIndices.spatialResamplingInputBufferIndex,
        inputReservoir,
        rng,
        runtimeParams,
        giParams.reservoirBufferParams,
        giParams.spatialResamplingParams);

    // Store spatial result (finalization is done inside the library)
    RTXDI_StoreGIReservoir(spatialResult,
        giParams.reservoirBufferParams,
        GlobalIndex,
        giParams.bufferIndices.spatialResamplingOutputBufferIndex);
}
