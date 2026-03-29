// ReSTIR GI Fused Spatiotemporal Resampling Compute Shader
// Single-pass combined temporal + spatial resampling.

#include "RtxdiApplicationBridge.hlsli"
#include <Rtxdi/GI/SpatioTemporalResampling.hlsli>
#include <Rtxdi/GI/BoilingFilter.hlsli>

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID)
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

    // Create initial GI reservoir from secondary surface data
    RTXDI_GIReservoir inputReservoir = RTXDI_EmptyGIReservoir();
    {
        float4 posNormal = SecondaryPositionNormal[GlobalIndex];
        float4 radPdf = SecondaryRadiance[GlobalIndex];

        if (radPdf.w > 0)
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

    // Store initial reservoir
    RTXDI_StoreGIReservoir(inputReservoir,
        giParams.reservoirBufferParams,
        GlobalIndex,
        giParams.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex);

    float3 motionVector = MotionVectors[GlobalIndex].xyz;
    motionVector.xy *= float2(Camera.RenderSize); // UV-space to pixel-space for RTXDI

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(GlobalIndex, runtimeParams.frameIndex, 2);

    // Jitter max reservoir age
    RTXDI_GISpatioTemporalResamplingParameters stparams = giParams.spatioTemporalResamplingParams;
    stparams.maxReservoirAge = uint(float(stparams.maxReservoirAge) * (0.5 + 0.5 * RTXDI_GetNextRandom(rng)));

    // Fused spatiotemporal resampling
    RTXDI_GIReservoir result = RTXDI_GISpatioTemporalResampling(
        GlobalIndex,
        surface,
        giParams.bufferIndices.secondarySurfaceReSTIRDIOutputBufferIndex,
        motionVector,
        inputReservoir,
        rng,
        runtimeParams,
        giParams.reservoirBufferParams,
        stparams);

#ifdef RTXDI_ENABLE_BOILING_FILTER
    if (giParams.boilingFilterParams.enableBoilingFilter)
    {
        RTXDI_GIBoilingFilter(LocalIndex, giParams.boilingFilterParams.boilingFilterStrength, result);
    }
#endif

    // Store result (finalization is done inside the library)
    RTXDI_StoreGIReservoir(result,
        giParams.reservoirBufferParams,
        GlobalIndex,
        giParams.bufferIndices.spatialResamplingOutputBufferIndex);
}
