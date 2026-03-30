// ReSTIR GI Final Shading Compute Shader
// Reads the final GI reservoir and applies the indirect illumination to the output.

#include "RtxdiApplicationBridge.hlsli"
#include <Rtxdi/GI/Reservoir.hlsli>
#include "include/ColorConversions.hlsli"

static const float kMaxBrdfValue = 1e4;
static const float kMISRoughness = 0.2;

float GetMISWeight(float3 roughBrdf, float3 trueBrdf)
{
    roughBrdf = clamp(roughBrdf, 1e-4, kMaxBrdfValue);
    trueBrdf = clamp(trueBrdf, 0, kMaxBrdfValue);
    const float initWeight = saturate(Color::RGBToLuminance(trueBrdf) / Color::RGBToLuminance(trueBrdf + roughBrdf));
    return initWeight * initWeight * initWeight;
}

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    if (any(GlobalIndex >= Camera.RenderSize))
        return;

    const RTXDI_GIParameters giParams = g_ReSTIRGI.giParams;
    const RTXDI_RuntimeParameters runtimeParams = g_ReSTIRGI.runtimeParams;

    RAB_Surface rab = RAB_GetGBufferSurface(GlobalIndex, false);

    if (!RAB_IsSurfaceValid(rab))
        return;

    // Load the final reservoir
    RTXDI_GIReservoir finalReservoir = RTXDI_LoadGIReservoir(
        giParams.reservoirBufferParams,
        GlobalIndex,
        giParams.bufferIndices.finalShadingInputBufferIndex);

    if (!RTXDI_IsValidGIReservoir(finalReservoir))
        return;

    // Reconstruct initial reservoir from secondary surface data
    RTXDI_GIReservoir initialReservoir = RTXDI_EmptyGIReservoir();
    {
        float4 posNormal = SecondaryPositionNormal[GlobalIndex];
        float4 radPdf = SecondaryRadiance[GlobalIndex];
        if (radPdf.w > 0)
        {
            float3 sampleNormal = DecodeNormal(half2(
                f16tof32(asuint(posNormal.w) & 0xFFFF),
                f16tof32(asuint(posNormal.w) >> 16)));
            initialReservoir = RTXDI_MakeGIReservoir(posNormal.xyz, sampleNormal, radPdf.xyz, radPdf.w);
        }
    }

    // Compute direction and distance to final reservoir sample
    float3 L = finalReservoir.position - rab.surface.Position;
    float hitDistance = length(L);
    L /= hitDistance;

    float3 finalRadiance = finalReservoir.radiance * finalReservoir.weightSum;

    // Optional final visibility check
    if (giParams.finalShadingParams.enableFinalVisibility)
    {
        if (!RAB_GetConservativeVisibility(rab, finalReservoir.position))
            finalRadiance = 0;
    }

    float4 bsdfEval = rab.Eval(L);

    // Delta chain throughput: accounts for mirror/glass reflectance along the delta path
    // before reaching the first non-delta surface. Stored as luminance in DiffuseAlbedo.a.
    float deltaThpLum = SecondaryDiffuseAlbedo[GlobalIndex].a;
    float deltaThp = (deltaThpLum > 0) ? deltaThpLum : 1.0;

    float3 finalGI;

    if (giParams.finalShadingParams.enableFinalMIS)
    {
        // Direction and BSDF for the initial sample
        float3 L0 = initialReservoir.position - rab.surface.Position;
        float hitDistance0 = length(L0);
        L0 /= hitDistance0;

        float4 bsdfEval0 = rab.Eval(L0);

        // Roughness-clamped BRDF for MIS weight computation
        float4 roughBrdf  = rab.EvalRoughnessClamp(kMISRoughness, L);
        float4 roughBrdf0 = rab.EvalRoughnessClamp(kMISRoughness, L0);

        const float finalWeight   = 1.0 - GetMISWeight(roughBrdf.rgb, bsdfEval.rgb);
        const float initialWeight = GetMISWeight(roughBrdf0.rgb, bsdfEval0.rgb);

        const float3 initialRadiance = initialReservoir.radiance * initialReservoir.weightSum;

        finalGI = deltaThp * (bsdfEval.rgb * finalRadiance * finalWeight + bsdfEval0.rgb * initialRadiance * initialWeight);
    }
    else
    {
        finalGI = deltaThp * bsdfEval.rgb * finalRadiance;
    }

    if (any(isinf(finalGI)) || any(isnan(finalGI)))
        finalGI = 0;

    // Read-modify-write MainTexture: reverse gamma, add GI, re-encode gamma
    float4 currentColor = OutputRadiance[GlobalIndex];
    float3 linearColor = LLGammaToTrueLinear(currentColor.rgb);
    linearColor += finalGI;
    OutputRadiance[GlobalIndex] = float4(LLTrueLinearToGamma(linearColor), currentColor.a);
}
