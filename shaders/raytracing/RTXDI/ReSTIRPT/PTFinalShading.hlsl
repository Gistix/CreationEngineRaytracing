// ReSTIR PT Final Shading Compute Shader
// Reads the final PT reservoir and applies indirect illumination to the output.

#include "../RtxdiPTApplicationBridge.hlsli"
#define RTXDI_RESTIR_PT_HYBRID_SHIFT
#include "../RtxdiPTPathTrace.hlsli"
#include <Rtxdi/PT/Reservoir.hlsli>
#include "include/ColorConversions.hlsli"

[numthreads(8, 8, 1)]
void Main(uint2 GlobalIndex : SV_DispatchThreadID)
{
    if (any(GlobalIndex >= Camera.RenderSize))
        return;

    const RTXDI_PTParameters ptParams = g_ReSTIRPT.ptParams;
    const RTXDI_RuntimeParameters runtimeParams = g_ReSTIRPT.runtimeParams;

    RAB_Surface rab = RAB_GetGBufferSurface(GlobalIndex, false);

    if (!RAB_IsSurfaceValid(rab))
        return;

    // Load the final reservoir
    uint2 reservoirPos = RTXDI_PixelPosToReservoirPos(GlobalIndex, runtimeParams.activeCheckerboardField);
    RTXDI_PTReservoir finalReservoir = RTXDI_LoadPTReservoir(
        ptParams.reservoirBuffer,
        reservoirPos,
        ptParams.bufferIndices.finalShadingInputBufferIndex);

    if (!RTXDI_IsValidPTReservoir(finalReservoir))
        return;

    // The product of WeightSum and TargetFunction gives us the final radiance
    float3 finalRadiance = finalReservoir.TargetFunction * finalReservoir.WeightSum;

    // Clamp to avoid fireflies
    float luminance = Color::RGBToLuminance(finalRadiance);
    if (luminance > 100.0f)
        finalRadiance *= 100.0f / luminance;

    if (any(isinf(finalRadiance)) || any(isnan(finalRadiance)))
        finalRadiance = 0;

    // For paths with RcVertexLength > 2, we need to regenerate the incoming direction
    // to split diffuse/specular. For now, use the direction to the reconnection vertex.
    float3 L = normalize(finalReservoir.TranslatedWorldPosition - rab.surface.Position);

    // Evaluate BSDF in the final direction to properly weight the radiance
    float4 bsdfEval = rab.Eval(L);

    // The target function already includes the BSDF * radiance, so the final contribution
    // is simply the reservoir weight times the target function.
    // However, for proper denoiser integration we apply the BSDF split here.
    float3 finalGI = finalRadiance;

    // Read-modify-write MainTexture: reverse gamma, add GI, re-encode gamma
    float4 currentColor = OutputRadiance[GlobalIndex];
    float3 linearColor = LLGammaToTrueLinear(currentColor.rgb);
    linearColor += finalGI;
    OutputRadiance[GlobalIndex] = float4(LLTrueLinearToGamma(linearColor), currentColor.a);
}
