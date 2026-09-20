#include "interop/CameraData.hlsli"
#include "interop/ASVGFData.hlsli"
#include "include/WaveSize.hlsli"
#include "include/ASVGF_Common.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<ASVGFData> ASVGF   : register(b1);

Texture2D<float4> CurrentRadiance        : register(t0);
Texture2D<float4> ReprojectedHistory     : register(t1);
Texture2D<float>  ReprojectedLength      : register(t2);
Texture2D<float2> FilteredGradient       : register(t3);
Texture2D<float>  DepthTexture           : register(t4);
Texture2D<float4> NormalRoughnessTexture : register(t5);

RWTexture2D<float4> OutAccumulatedRadiance   : register(u0);
RWTexture2D<float>  OutVariance              : register(u1);
RWTexture2D<float4> OutNewHistoryRadiance    : register(u2);
RWTexture2D<float4> OutNewHistoryNormalDepth : register(u3);
RWTexture2D<float>  OutNewHistoryLength      : register(u4);

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixelPos = dispatchThreadID.xy;
    if (any(pixelPos >= (uint2)ASVGF.RenderSize))
        return;

    const float4 currentSample = CurrentRadiance[pixelPos];
    const float currentDepth = DepthTexture[pixelPos];
    const float4 currentNormalRough = NormalRoughnessTexture[pixelPos];
    const float lenN = length(currentNormalRough.xyz);
    const float3 currentNormal = lenN > 1e-3f ? (currentNormalRough.xyz / lenN) : float3(0.0f, 0.0f, 1.0f);

    const float currentLuma = ASVGF_Luminance(currentSample.rgb);
    const float currentSecondMoment = currentLuma * currentLuma;

    const float4 reprojectedHistory = ReprojectedHistory[pixelPos];
    const float reprojectedLength = ReprojectedLength[pixelPos];
    const float2 filteredGrad = FilteredGradient[pixelPos];

    const float meanGrad = filteredGrad.x;
    const float gradStdDev = filteredGrad.y;

    // Temporal gradient signal-to-noise ratio:
    // Subtract a 1.5-sigma noise deadband so ordinary Monte Carlo variance is not mistaken for a lighting change.
    const float lumaNorm = max(currentLuma, 0.05f);
    const float excessGrad = max(0.0f, abs(meanGrad) - 1.5f * gradStdDev);
    const float snrGrad = excessGrad / (gradStdDev + 0.1f * lumaNorm + 1e-3f);
    const float changeMetric = saturate(snrGrad * ASVGF.GradientSensitivity);

    // Base temporal blend factor based on accumulated frames
    const float baseAlpha = max(1.0f / (reprojectedLength + 1.0f), ASVGF.TemporalAlphaMin);

    // Modulate alpha adaptively: if illumination change detected, increase alpha smoothly up to 0.35
    float alpha = lerp(baseAlpha, min(0.35f, ASVGF.TemporalAlphaMax), changeMetric);

    if (reprojectedLength <= 0.0f)
        alpha = 1.0f;

    // Temporal accumulation of radiance and 2nd moment
    const float3 accumColor = lerp(reprojectedHistory.rgb, currentSample.rgb, alpha);
    const float accumSecondMoment = lerp(reprojectedHistory.a, currentSecondMoment, alpha);

    // Temporal variance: Var = E[X^2] - (E[X])^2
    const float accumLuma = ASVGF_Luminance(accumColor);
    const float temporalVariance = max(0.0f, accumSecondMoment - accumLuma * accumLuma);

    // Update history length: smooth decay instead of hard drop to 0, but clamp to 1.0 if history was invalid
    float newLength = lerp(min(reprojectedLength + 1.0f, (float)ASVGF.MaxHistoryLength), 4.0f, changeMetric);
    if (reprojectedLength <= 0.0f)
        newLength = 1.0f;

    // Store new history
    OutNewHistoryRadiance[pixelPos] = float4(accumColor, accumSecondMoment);
    OutNewHistoryNormalDepth[pixelPos] = float4(currentNormal, currentDepth);
    OutNewHistoryLength[pixelPos] = newLength;

    // Output to spatial filtering stages
    OutAccumulatedRadiance[pixelPos] = float4(accumColor, currentSample.w); // preserve hit distance in w
    OutVariance[pixelPos] = temporalVariance;
}
