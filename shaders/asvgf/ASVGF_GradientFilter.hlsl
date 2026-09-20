#include "interop/CameraData.hlsli"
#include "interop/ASVGFData.hlsli"
#include "include/WaveSize.hlsli"
#include "include/ASVGF_Common.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<ASVGFData> ASVGF   : register(b1);

Texture2D<float2> TemporalGradient       : register(t0);
Texture2D<float>  DepthTexture           : register(t1);
Texture2D<float4> NormalRoughnessTexture : register(t2);

RWTexture2D<float2> OutFilteredGradient : register(u0);

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const int2 pixelPos = int2(dispatchThreadID.xy);
    const int2 maxPos = int2(ASVGF.RenderSize) - 1;

    if (any(pixelPos > maxPos))
        return;

    const float centerDepth = DepthTexture[pixelPos];
    const float3 centerNormal = normalize(NormalRoughnessTexture[pixelPos].xyz);

    float2 gradSum = 0.0f;
    float weightSum = 0.0f;

    // 3x3 edge-preserving cross-bilateral filter on temporal gradient
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 tapPos = clamp(pixelPos + int2(dx, dy), int2(0, 0), maxPos);
            const float tapDepth = DepthTexture[tapPos];
            const float3 tapNormal = normalize(NormalRoughnessTexture[tapPos].xyz);

            const float wDepth = ASVGF_DepthWeight(centerDepth, tapDepth, ASVGF.DepthSigma);
            const float wNormal = ASVGF_NormalWeight(centerNormal, tapNormal, ASVGF.NormalSigma);
            const float wKernel = (dx == 0 && dy == 0) ? 0.5f : ((dx == 0 || dy == 0) ? 0.25f : 0.125f);
            const float w = wDepth * wNormal * wKernel;

            gradSum += TemporalGradient[tapPos] * w;
            weightSum += w;
        }
    }

    float2 filteredGradMoments = TemporalGradient[pixelPos];
    if (weightSum > 1e-4f)
        filteredGradMoments = gradSum / weightSum;

    const float meanGrad = filteredGradMoments.x;
    const float secondMomentGrad = filteredGradMoments.y;
    const float gradVariance = max(0.0f, secondMomentGrad - meanGrad * meanGrad);
    const float gradStdDev = sqrt(gradVariance);

    OutFilteredGradient[pixelPos] = float2(meanGrad, gradStdDev);
}
