#include "interop/CameraData.hlsli"
#include "interop/ASVGFData.hlsli"
#include "include/WaveSize.hlsli"
#include "include/ASVGF_Common.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<ASVGFData> ASVGF   : register(b1);

Texture2D<float4> RadianceTexture         : register(t0);
Texture2D<float>  TemporalVarianceTexture : register(t1);
Texture2D<float>  HistoryLengthTexture    : register(t2);
Texture2D<float>  DepthTexture            : register(t3);
Texture2D<float4> NormalRoughnessTexture  : register(t4);

RWTexture2D<float> OutVariance : register(u0);

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const int2 pixelPos = int2(dispatchThreadID.xy);
    const int2 maxPos = int2(ASVGF.RenderSize) - 1;

    if (any(pixelPos > maxPos))
        return;

    const float historyLen = HistoryLengthTexture[pixelPos];
    const float temporalVar = TemporalVarianceTexture[pixelPos];

    // If accumulated history is high enough, temporal variance is reliable
    if (historyLen >= 4.0f)
    {
        OutVariance[pixelPos] = temporalVar;
        return;
    }

    const float centerDepth = DepthTexture[pixelPos];
    const float3 centerNormalRaw = NormalRoughnessTexture[pixelPos].xyz;
    const float lenCenterN = length(centerNormalRaw);
    const float3 centerNormal = lenCenterN > 1e-3f ? (centerNormalRaw / lenCenterN) : float3(0.0f, 0.0f, 1.0f);

    float meanLuma = 0.0f;
    float meanLumaSq = 0.0f;
    float weightSum = 0.0f;

    // 3x3 local cross-bilateral neighborhood to calculate spatial sample variance
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 tapPos = clamp(pixelPos + int2(dx, dy), int2(0, 0), maxPos);
            const float tapDepth = DepthTexture[tapPos];
            const float3 tapNormalRaw = NormalRoughnessTexture[tapPos].xyz;
            const float lenTapN = length(tapNormalRaw);
            const float3 tapNormal = lenTapN > 1e-3f ? (tapNormalRaw / lenTapN) : float3(0.0f, 0.0f, 1.0f);

            const float wDepth = ASVGF_DepthWeight(centerDepth, tapDepth, ASVGF.DepthSigma);
            const float wNormal = ASVGF_NormalWeight(centerNormal, tapNormal, ASVGF.NormalSigma);
            const float w = wDepth * wNormal;

            const float luma = ASVGF_Luminance(RadianceTexture[tapPos].rgb);

            meanLuma += luma * w;
            meanLumaSq += (luma * luma) * w;
            weightSum += w;
        }
    }

    float spatialVar = 0.0f;
    if (weightSum > 1e-4f)
    {
        meanLuma /= weightSum;
        meanLumaSq /= weightSum;
        spatialVar = max(0.0f, meanLumaSq - meanLuma * meanLuma);
    }

    // Blend spatial variance with temporal variance based on accumulated history
    const float historyWeight = saturate(historyLen / 4.0f);
    OutVariance[pixelPos] = max(lerp(spatialVar, temporalVar, historyWeight), 1e-4f);
}
