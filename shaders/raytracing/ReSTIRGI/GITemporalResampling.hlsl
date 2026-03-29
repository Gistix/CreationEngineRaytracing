// ReSTIR GI Temporal Resampling Compute Shader
// Creates initial GI reservoirs from secondary surface data and applies temporal resampling.

#include "RtxdiApplicationBridge.hlsli"
#include <Rtxdi/GI/TemporalResampling.hlsli>
#include <Rtxdi/GI/BoilingFilter.hlsli>

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID)
{
    if (any(GlobalIndex >= Camera.RenderSize))
        return;

    const RTXDI_GIParameters giParams = g_ReSTIRGI.giParams;
    const RTXDI_RuntimeParameters runtimeParams = g_ReSTIRGI.runtimeParams;

    // Get the current surface from G-buffer
    RAB_Surface surface = RAB_GetGBufferSurface(GlobalIndex, false);

    if (!RAB_IsSurfaceValid(surface))
    {
        // No valid surface: store empty reservoir
        RTXDI_StoreGIReservoir(RTXDI_EmptyGIReservoir(),
            giParams.reservoirBufferParams,
            GlobalIndex,
            giParams.bufferIndices.temporalResamplingOutputBufferIndex);
        return;
    }

    // Create initial GI reservoir from secondary surface data
    RTXDI_GIReservoir inputReservoir = RTXDI_EmptyGIReservoir();
    {
        float4 posNormal = SecondaryPositionNormal[GlobalIndex];
        float4 radPdf = SecondaryRadiance[GlobalIndex];

        if (radPdf.w > 0) // Valid sample (pdf > 0)
        {
            float3 samplePos = posNormal.xyz;
            float3 sampleNormal = DecodeNormal(half2(
                f16tof32(asuint(posNormal.w) & 0xFFFF),
                f16tof32(asuint(posNormal.w) >> 16)));
            float3 sampleRadiance = radPdf.xyz;
            float samplePdf = radPdf.w;

            inputReservoir = RTXDI_MakeGIReservoir(samplePos, sampleNormal, sampleRadiance, samplePdf);
        }
    }

    // Store the initial reservoir for this frame at the secondary surface output buffer slot
    RTXDI_StoreGIReservoir(inputReservoir,
        giParams.reservoirBufferParams,
        GlobalIndex,
        giParams.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

    // Read motion vector and convert from UV-space to pixel-space for RTXDI
    float3 motionVector = MotionVectors[GlobalIndex].xyz;
    motionVector.xy *= float2(Camera.RenderSize);

    // Initialize random sampler
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(GlobalIndex, runtimeParams.frameIndex, 0);

    // Jitter max reservoir age to avoid large areas of reservoirs dying at the same time
    RTXDI_GITemporalResamplingParameters tparams = giParams.temporalResamplingParams;
    tparams.maxReservoirAge = uint(float(tparams.maxReservoirAge) * (0.5 + 0.5 * RTXDI_GetNextRandom(rng)));

    // Perform temporal resampling
    RTXDI_GIReservoir temporalResult = RTXDI_GITemporalResampling(
        GlobalIndex,
        surface,
        motionVector,
        giParams.bufferIndices.temporalResamplingInputBufferIndex,
        inputReservoir,
        rng,
        runtimeParams,
        giParams.reservoirBufferParams,
        tparams);

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (giParams.boilingFilterParams.enableBoilingFilter)
    {
        RTXDI_GIBoilingFilter(LocalIndex, giParams.boilingFilterParams.boilingFilterStrength, temporalResult);
    }
#endif

    // Store temporal result (finalization is done inside the library)
    RTXDI_StoreGIReservoir(temporalResult,
        giParams.reservoirBufferParams,
        GlobalIndex,
        giParams.bufferIndices.temporalResamplingOutputBufferIndex);
}
