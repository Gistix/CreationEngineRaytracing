// ReSTIR GI Final Shading Compute Shader
// Reads the final GI reservoir and applies the indirect illumination to the output.

#include "RtxdiApplicationBridge.hlsli"
#include <Rtxdi/GI/Reservoir.hlsli>
#include "include/ColorConversions.hlsli"

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

    float3 giRadiance = finalReservoir.radiance;
    float giWeight = finalReservoir.weightSum;

    // Optional final visibility check
    if (giParams.finalShadingParams.enableFinalVisibility && giWeight > 0)
    {
        if (!RAB_GetConservativeVisibility(rab, finalReservoir.position))
        {
            giWeight = 0;
            giRadiance = 0;
        }
    }

    if (giWeight <= 0)
        return;

    // Compute the GI contribution using the Surface's BSDF evaluation
    // rab.Eval() returns float4 where rgb = BSDF value (includes NdotL via tangent-space cosine)
    float3 L = normalize(finalReservoir.position - rab.surface.Position);
    float4 bsdfEval = rab.Eval(L);

    // Delta chain throughput: accounts for mirror/glass reflectance along the delta path
    // before reaching the first non-delta surface. Stored as luminance in DiffuseAlbedo.a.
    float deltaThpLum = SecondaryDiffuseAlbedo[GlobalIndex].a;
    float deltaThp = (deltaThpLum > 0) ? deltaThpLum : 1.0;

    // Final GI = deltaThp * BSDF(wi, wo) * radiance * RIS weight
    float3 finalGI = deltaThp * bsdfEval.rgb * giRadiance * giWeight;

    // Optional final MIS: blend with the initial sample for reduced variance
    if (giParams.finalShadingParams.enableFinalMIS)
    {
        float4 radPdf = SecondaryRadiance[GlobalIndex];
        float3 initialRadiance = radPdf.xyz;
        float initialPdf = radPdf.w;

        if (initialPdf > 0)
        {
            float targetPdf = RAB_GetGISampleTargetPdfForSurface(finalReservoir.position, finalReservoir.radiance, rab);
            float risWeight = targetPdf * finalReservoir.weightSum;

            // Balance heuristic MIS weight
            float misWeight = (risWeight > 0) ? risWeight / (risWeight + initialPdf) : 0;
            float misWeightInitial = 1.0 - misWeight;

            // Initial sample also uses full BSDF evaluation
            float3 initialGI = deltaThp * bsdfEval.rgb * initialRadiance;
            finalGI = finalGI * misWeight + initialGI * misWeightInitial;
        }
    }

    // Read-modify-write MainTexture: reverse gamma, add GI, re-encode gamma
    float4 currentColor = OutputRadiance[GlobalIndex];
    float3 linearColor = LLGammaToTrueLinear(currentColor.rgb);
    linearColor += finalGI;
    OutputRadiance[GlobalIndex] = float4(LLTrueLinearToGamma(linearColor), currentColor.a);
}
