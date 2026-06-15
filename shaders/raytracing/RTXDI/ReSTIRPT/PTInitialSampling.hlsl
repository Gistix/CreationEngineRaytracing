// ReSTIR PT Initial Sampling Compute Shader

#include "../RtxdiPTApplicationBridge.hlsli"
#define RTXDI_RESTIR_PT_INITIAL_SAMPLING
#include "../RtxdiPTPathTrace.hlsli"
#include <Rtxdi/PT/InitialSampling.hlsli>
#include <Rtxdi/Utils/RandomSamplerPerPassSeeds.hlsli>

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    if (any(GlobalIndex >= g_ReSTIRPT.renderSize))
        return;

    const RTXDI_PTParameters ptParams = g_ReSTIRPT.ptParams;
    const RTXDI_RuntimeParameters runtimeParams = g_ReSTIRPT.runtimeParams;

    RAB_Surface surface = RAB_GetGBufferSurface(GlobalIndex, false);
    uint2 reservoirPos = RTXDI_PixelPosToReservoirPos(GlobalIndex, runtimeParams.activeCheckerboardField);

    if (!RAB_IsSurfaceValid(surface))
    {
        RTXDI_StorePTReservoir(RTXDI_EmptyPTReservoir(), ptParams.reservoirBuffer, reservoirPos, ptParams.bufferIndices.initialPathTracerOutputBufferIndex);
        return;
    }

    RTXDI_PathTracerRandomContext ptRandContext = RTXDI_InitializePathTracerRandomContext(
        GlobalIndex,
        runtimeParams.frameIndex,
        RTXDI_PT_GENERATE_INITIAL_SAMPLES_RANDOM_SEED,
        RTXDI_PT_GENERATE_INITIAL_SAMPLES_REPLAY_RANDOM_SEED);

    RTXDI_PTInitialSamplingRuntimeParameters isrParams = RTXDI_EmptyPTInitialSamplingRuntimeParameters();
    isrParams.cameraPos = Camera.Position.xyz;
    isrParams.prevCameraPos = Camera.PositionPrev.xyz;
    isrParams.prevPrevCameraPos = Camera.PositionPrev.xyz;

    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData(GlobalIndex);

    RTXDI_PTReservoir reservoir = GenerateInitialSamples(
        ptParams.initialSampling,
        isrParams,
        ptParams.reconnection,
        ptRandContext,
        surface,
        ptud);

    RTXDI_StorePTReservoir(reservoir, ptParams.reservoirBuffer, reservoirPos, ptParams.bufferIndices.initialPathTracerOutputBufferIndex);
}
