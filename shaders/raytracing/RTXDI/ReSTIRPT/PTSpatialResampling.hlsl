// ReSTIR PT Spatial Resampling Compute Shader

#include "../RtxdiPTApplicationBridge.hlsli"
#define RTXDI_RESTIR_PT_HYBRID_SHIFT
#include "../RtxdiPTPathTrace.hlsli"
#include <Rtxdi/PT/SpatialResampling.hlsli>

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    if (any(GlobalIndex >= Camera.RenderSize))
        return;

    const RTXDI_PTParameters ptParams = g_ReSTIRPT.ptParams;
    const RTXDI_RuntimeParameters runtimeParams = g_ReSTIRPT.runtimeParams;

    uint2 reservoirPos = RTXDI_PixelPosToReservoirPos(GlobalIndex, runtimeParams.activeCheckerboardField);

    RTXDI_PTSpatialResamplingRuntimeParameters srrParams = RTXDI_EmptyPTSpatialResamplingRuntimeParameters();
    srrParams.PixelPosition = GlobalIndex;
    srrParams.ReservoirPosition = reservoirPos;
    srrParams.cameraPos = Camera.Position.xyz;
    srrParams.prevCameraPos = Camera.PrevPosition.xyz;
    srrParams.prevPrevCameraPos = Camera.PrevPosition.xyz;

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(GlobalIndex, runtimeParams.frameIndex, 4);

    RAB_PathTracerUserData ptud;
    ptud.pathType = 0;
    ptud.pixelPosition = GlobalIndex;
    ptud.psrValid = 0;

    bool resampled = false;

    RTXDI_PTReservoir result = RTXDI_PTSpatialResampling(
        srrParams,
        ptParams.spatialResampling,
        ptParams.hybridShift,
        ptParams.reconnection,
        ptParams.reservoirBuffer,
        ptParams.bufferIndices,
        runtimeParams,
        rng,
        resampled,
        ptud);

    RTXDI_StorePTReservoir(result, ptParams.reservoirBuffer, reservoirPos, ptParams.bufferIndices.spatialResamplingOutputBufferIndex);
}
