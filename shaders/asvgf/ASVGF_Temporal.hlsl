#include "interop/CameraData.hlsli"
#include "interop/ASVGFData.hlsli"
#include "include/WaveSize.hlsli"
#include "include/ASVGF_Common.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<ASVGFData> ASVGF   : register(b1);

Texture2D<float4> CurrentRadiance        : register(t0);
Texture2D<float>  CurrentDepth           : register(t1);
Texture2D<float4> CurrentNormalRoughness : register(t2);
Texture2D<float2> MotionVectors          : register(t3);
Texture2D<float4> HistoryRadianceMoments : register(t4);
Texture2D<float4> HistoryNormalDepth     : register(t5);
Texture2D<float>  HistoryLength          : register(t6);

SamplerState PointClampSampler  : register(s0);
SamplerState LinearClampSampler : register(s1);

RWTexture2D<float4> OutReprojectedHistory : register(u0);
RWTexture2D<float>  OutReprojectedLength  : register(u1);
RWTexture2D<float2> OutTemporalGradient   : register(u2);

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixelPos = dispatchThreadID.xy;
    if (any(pixelPos >= (uint2)ASVGF.RenderSize))
        return;

    const float2 uv = (float2(pixelPos) + 0.5f) * ASVGF.InvRenderSize;

    const float currentDepth = CurrentDepth[pixelPos];
    const float4 currentNormalRough = CurrentNormalRoughness[pixelPos];
    const float lenN = length(currentNormalRough.xyz);
    const float3 currentNormal = lenN > 1e-3f ? (currentNormalRough.xyz / lenN) : float3(0.0f, 0.0f, 1.0f);
    const float4 currentRadSample = CurrentRadiance[pixelPos];
    const float currentLuma = ASVGF_Luminance(currentRadSample.rgb);

    // Sky or invalid depth check
    if (currentDepth <= 0.0f || currentDepth >= 1e7f)
    {
        OutReprojectedHistory[pixelPos] = float4(currentRadSample.rgb, currentLuma * currentLuma);
        OutReprojectedLength[pixelPos] = 0.0f;
        OutTemporalGradient[pixelPos] = float2(0.0f, 0.0f);
        return;
    }

    const float2 motion = MotionVectors[pixelPos];
    const float2 prevUV = uv + motion;

    // Out of bounds check
    if (any(prevUV < 0.0f) || any(prevUV > 1.0f))
    {
        OutReprojectedHistory[pixelPos] = float4(currentRadSample.rgb, currentLuma * currentLuma);
        OutReprojectedLength[pixelPos] = 0.0f;
        OutTemporalGradient[pixelPos] = float2(0.0f, 0.0f);
        return;
    }

    // Bilinear tap of 4 history neighbors with geometric validation
    const float2 prevCoord = prevUV * ASVGF.RenderSize - 0.5f;
    const int2 baseCoord = int2(floor(prevCoord));
    const float2 fracCoord = frac(prevCoord);

    const float bilinearWeights[4] = {
        (1.0f - fracCoord.x) * (1.0f - fracCoord.y),
        fracCoord.x * (1.0f - fracCoord.y),
        (1.0f - fracCoord.x) * fracCoord.y,
        fracCoord.x * fracCoord.y
    };

    float4 sumHistory = 0.0f;
    float sumLength = 0.0f;
    float sumWeight = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 tapCoord = clamp(baseCoord + int2(i & 1, i >> 1), int2(0, 0), int2(ASVGF.RenderSize) - 1);
        const float4 histNormDepth = HistoryNormalDepth[tapCoord];
        const float3 histNormal = histNormDepth.xyz;
        const float histDepth = histNormDepth.w;

        const float dotN = saturate(dot(currentNormal, histNormal));
        const float wNormal = pow(dotN, 16.0f);
        const float relDepthDiff = abs(currentDepth - histDepth) / max(currentDepth, 1.0f);
        const float wDepth = exp(-relDepthDiff / max(ASVGF.DepthSigma * 0.08f, 0.02f));
        const float wGeom = (dotN > 0.7f && relDepthDiff < 0.15f) ? (wNormal * wDepth * bilinearWeights[i]) : 0.0f;

        if (wGeom > 1e-4f)
        {
            sumHistory += HistoryRadianceMoments[tapCoord] * wGeom;
            sumLength += HistoryLength[tapCoord] * wGeom;
            sumWeight += wGeom;
        }
    }

    float4 reprojectedHistory = float4(currentRadSample.rgb, currentLuma * currentLuma);
    float reprojectedLength = 0.0f;

    if (sumWeight > 1e-3f)
    {
        reprojectedHistory = sumHistory / sumWeight;
        reprojectedLength = sumLength / sumWeight;
    }

    // Compute raw temporal difference
    const float histLuma = ASVGF_Luminance(reprojectedHistory.rgb);
    const float rawGrad = currentLuma - histLuma;

    OutReprojectedHistory[pixelPos] = reprojectedHistory;
    OutReprojectedLength[pixelPos] = reprojectedLength;
    OutTemporalGradient[pixelPos] = float2(rawGrad, rawGrad * rawGrad);
}
