#include "interop/CameraData.hlsli"
#include "interop/ASVGFData.hlsli"
#include "include/WaveSize.hlsli"
#include "include/Vulkan.hlsli"
#include "include/ASVGF_Common.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<ASVGFData> ASVGF   : register(b1);
VK_PUSH_CONSTANT ConstantBuffer<AtrousPushConstants> PC : register(b2);

Texture2D<float4> InputRadiance          : register(t0);
Texture2D<float>  InputVariance          : register(t1);
Texture2D<float>  DepthTexture           : register(t2);
Texture2D<float4> NormalRoughnessTexture : register(t3);

RWTexture2D<float4> OutFilteredRadiance : register(u0);
RWTexture2D<float>  OutFilteredVariance : register(u1);

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const int2 pixelPos = int2(dispatchThreadID.xy);
    const int2 maxPos = int2(ASVGF.RenderSize) - 1;

    if (any(pixelPos > maxPos))
        return;

    const float4 centerRadiance = InputRadiance[pixelPos];
    const float centerLuma = ASVGF_Luminance(centerRadiance.rgb);
    const float centerDepth = DepthTexture[pixelPos];
    const float4 centerNormalRough = NormalRoughnessTexture[pixelPos];
    const float lenCenterN = length(centerNormalRough.xyz);
    const float3 centerNormal = lenCenterN > 1e-3f ? (centerNormalRough.xyz / lenCenterN) : float3(0.0f, 0.0f, 1.0f);

    // Sky or invalid depth pass-through
    if (centerDepth <= 0.0f || centerDepth >= 1e7f)
    {
        OutFilteredRadiance[pixelPos] = centerRadiance;
        OutFilteredVariance[pixelPos] = 0.0f;
        return;
    }

    const float centerVariance = InputVariance[pixelPos];
    const float centerStdDev = sqrt(max(0.0f, centerVariance));

    const int stepSize = PC.StepSize;

    float4 sumRadiance = 0.0f;
    float sumVariance = 0.0f;
    float sumWeight = 0.0f;
    float sumWeightSq = 0.0f;

    // 5x5 A-trous wavelet kernel
    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        const float wy = ASVGF_Kernel1D[dy + 2];

        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            const float wx = ASVGF_Kernel1D[dx + 2];
            const float kernelWeight = wx * wy;

            const int2 tapPos = clamp(pixelPos + int2(dx, dy) * stepSize, int2(0, 0), maxPos);

            const float4 tapRadiance = InputRadiance[tapPos];
            const float tapLuma = ASVGF_Luminance(tapRadiance.rgb);
            const float tapDepth = DepthTexture[tapPos];
            const float4 tapNormalRough = NormalRoughnessTexture[tapPos];
            const float lenTapN = length(tapNormalRough.xyz);
            const float3 tapNormal = lenTapN > 1e-3f ? (tapNormalRough.xyz / lenTapN) : float3(0.0f, 0.0f, 1.0f);
            const float tapVariance = InputVariance[tapPos];

            const float wDepth = ASVGF_DepthWeight(centerDepth, tapDepth, ASVGF.DepthSigma, (float)stepSize);
            const float wNormal = ASVGF_NormalWeight(centerNormal, tapNormal, ASVGF.NormalSigma);
            const float wLuma = ASVGF_LuminanceWeight(centerLuma, tapLuma, centerStdDev, ASVGF.LuminanceSigma);

            const float weight = kernelWeight * wDepth * wNormal * wLuma;

            sumRadiance += tapRadiance * weight;
            sumVariance += tapVariance * (weight * weight);
            sumWeight += weight;
            sumWeightSq += weight * weight;
        }
    }

    float4 filteredRadiance = centerRadiance;
    float filteredVariance = centerVariance;

    if (sumWeight > 1e-4f)
    {
        filteredRadiance = sumRadiance / sumWeight;
        filteredVariance = max(sumVariance / (sumWeight * sumWeight), 1e-4f);
    }

    // Preserve hit distance in w channel
    filteredRadiance.w = centerRadiance.w;

    OutFilteredRadiance[pixelPos] = filteredRadiance;
    OutFilteredVariance[pixelPos] = filteredVariance;
}
