// ReSTIR PT Temporal Resampling Compute Shader

#include "../RtxdiPTApplicationBridge.hlsli"
#include <Rtxdi/PT/TemporalResampling.hlsli>
#include <Rtxdi/PT/BoilingFilter.hlsli>

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID, uint LocalIndex : SV_GroupIndex)
{
    if (any(GlobalIndex >= Camera.RenderSize))
        return;

    const RTXDI_PTParameters ptParams = g_ReSTIRPT.ptParams;
    const RTXDI_RuntimeParameters runtimeParams = g_ReSTIRPT.runtimeParams;

    uint2 reservoirPos = RTXDI_PixelPosToReservoirPos(GlobalIndex, runtimeParams.activeCheckerboardField);

    // Load motion vector for temporal reprojection
    float4 mv = MotionVectors[GlobalIndex];

    RTXDI_PTTemporalResamplingRuntimeParameters trrParams = RTXDI_EmptyPTTemporalResamplingRuntimeParameters();
    trrParams.pixelPosition = GlobalIndex;
    trrParams.reservoirPosition = reservoirPos;
    trrParams.motionVector = float3(mv.xy * float2(Camera.RenderSize), mv.z);
    trrParams.cameraPos = Camera.Position.xyz;
    trrParams.prevCameraPos = Camera.PrevPosition.xyz;
    trrParams.prevPrevCameraPos = Camera.PrevPosition.xyz; // No prev-prev tracking yet

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(GlobalIndex, runtimeParams.frameIndex, 3);

    RAB_PathTracerUserData ptud;
    ptud.pathType = 0;

    bool selectedPrevSample = false;

    RTXDI_PTReservoir result = RTXDI_PTTemporalResampling(
        ptParams.temporalResampling,
        trrParams,
        ptParams.hybridShift,
        ptParams.reconnection,
        runtimeParams,
        ptParams.reservoirBuffer,
        rng,
        ptParams.bufferIndices,
        selectedPrevSample,
        ptud);

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (ptParams.boilingFilter.enableBoilingFilter)
    {
        RTXDI_PTBoilingFilter(LocalIndex, ptParams.boilingFilter.boilingFilterStrength, result);
    }
#endif

    RTXDI_StorePTReservoir(result, ptParams.reservoirBuffer, reservoirPos, ptParams.bufferIndices.temporalResamplingOutputBufferIndex);
}
