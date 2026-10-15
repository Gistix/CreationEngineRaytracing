#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<FeatureData> Features : register(b1);
ConstantBuffer<RaytracingData> Raytracing : register(b2);

Texture2DArray<float4> DiffuseSH : register(t0);
Texture2DArray<float4> SpecularSH : register(t1);
Texture2D<float> LowResDepth : register(t2);
Texture2D<float> FullResDepth : register(t3);
Texture2D<float3> Albedo : register(t4);
Texture2D<float4> FullResNormalRoughness : register(t5);
Texture2D<float4> MotionVectors : register(t6); // debug only: downscaled MVs (UV deltas)

RWTexture2D<float4> Output : register(u0);

SamplerState LinearClampSampler : register(s0);
SamplerState PointClampSampler : register(s1);

#include "include/ColorConversions.hlsli"
#include "include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "include/SH.hlsli"

static const float kJBU_DepthSigma = 0.01f;

void SampleJBUSlices(
    Texture2DArray<float4> shTex,
    int2 base,
    float4 weights,
    int2 maxTexel,
    out float4 slices[7])
{
    [unroll]
    for (uint s = 0; s < 7; s++)
        slices[s] = 0.0f;

    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        const int2 texel = clamp(base + int2(i & 1, i >> 1), 0, maxTexel);

        [unroll]
        for (uint s2 = 0; s2 < 7; s2++)
            slices[s2] += shTex.Load(int4(texel, (int)s2, 0)) * weights[i];
    }
}

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 size = Camera.RenderSize;

    if (any(idx >= size))
        return;

#ifdef SH_DEBUG_MV
    // Diagnostic: motion-vector flow field. Static camera must be black;
    // rotation must show a smooth gradient across the screen. Black during
    // motion means no vectors reach the temporal pass.
    const float2 screenUVDbg = (float2(idx) + 0.5f) / float2(Camera.ScreenSize);
    const float2 giUVDbg = screenUVDbg * Raytracing.ResolutionScale;
    const float2 mvDbg = MotionVectors.SampleLevel(PointClampSampler, giUVDbg, 0).xy;
    const float2 pixelMvDbg = mvDbg * float2(Camera.RenderSize);
    const float mvLen = length(pixelMvDbg);
    const float2 mvDir = mvLen > 1e-6f ? pixelMvDbg / mvLen : 0.0f;
    const float heat = saturate(mvLen / 32.0f);
    Output[idx] = float4(mvDir * 0.5f + 0.5f, heat, 1.0f);
    return;
#endif

    // Textures are screen-sized; the engine populates the G-buffer in the
    // top-left RenderSize region, while SH occupies the top-left
    // ceil(RenderSize * ResolutionScale) region.
    const float2 screenUV = (float2(idx) + 0.5f) / float2(Camera.ScreenSize);
    const float2 giUV = screenUV * Raytracing.ResolutionScale;

    const int2 maxTexel = int2(ceil(float2(Camera.RenderSize) * Raytracing.ResolutionScale)) - 1;
    const float2 samplePos = giUV * float2(Camera.ScreenSize) - 0.5f;
    const int2 base = int2(floor(samplePos));
    const float2 f = frac(samplePos);

    float4 bilinearWeights =
    {
        (1.0f - f.x) * (1.0f - f.y),
        (f.x) * (1.0f - f.y),
        (1.0f - f.x) * (f.y),
        (f.x) * (f.y)
    };

    // Joint bilateral weights from full-res vs low-res view depth. One
    // weight set is shared by all SH coefficients of both lobes.
    const float depth0 = ScreenToViewDepth(FullResDepth[idx], Camera.CameraData);
    const float sigma = max(kJBU_DepthSigma * depth0, 1e-6f);

    float4 weights;
    float weightSum = 0.0f;

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        const int2 texel = clamp(base + int2(i & 1, i >> 1), int2(0, 0), maxTexel);
        const float depth1 = LowResDepth[texel];

        const float depthWeight = exp(-abs(depth0 - depth1) / sigma);

        weights[i] = depthWeight * bilinearWeights[i];
        weightSum += weights[i];
    }

    weights /= max(weightSum, 1e-6f);

    float4 diffSlices[7];
    float4 specSlices[7];
    SampleJBUSlices(DiffuseSH, base, weights, maxTexel, diffSlices);
    SampleJBUSlices(SpecularSH, base, weights, maxTexel, specSlices);

    float3 diffSH[9];
    float3 specSH[9];
    float extra0, extra1;
    SHUnpack(diffSlices, diffSH, extra0);
    SHUnpack(specSlices, specSH, extra1);

    // Decode with the full-res shading normal (world space, matching the
    // incident directions stored by the GI pass). Sparse SH Reconstruction
    // can ring negative; fall back to the always non-negative L0 ambient
    // instead of clipping to black.
    const float3 normal = FullResNormalRoughness.SampleLevel(PointClampSampler, screenUV, 0).xyz;

    const float3 diffuseIrradiance = max(SHResolveCoeffs(diffSH, normal), diffSH[0] * 0.282095f);
    const float3 specularRadiance = max(SHResolveCoeffs(specSH, normal), specSH[0] * 0.282095f);

    // The GI pass outputs demodulated diffuse (first-bounce albedo divided
    // out); re-apply the full-res albedo here to keep texture detail sharp.
    const float3 albedo = LLGammaToTrueLinear(Albedo.SampleLevel(PointClampSampler, screenUV, 0).rgb);

    Output[idx] = float4(diffuseIrradiance * albedo + specularRadiance, 1.0f);
}
