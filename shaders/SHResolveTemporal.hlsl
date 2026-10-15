#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"

#include "include/SH.hlsli"
#include "include/WaveSize.hlsli"
#include "include/Vulkan.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<RaytracingData> Raytracing : register(b1);

struct SHResolveConstants
{
    float BlendAlpha; // 1 / min(accumulatedFrames + 1, maxFrames)
};

VK_PUSH_CONSTANT ConstantBuffer<SHResolveConstants> PC : register(b2);

Texture2D<float4> DiffuseRadiance : register(t0); // rgb: demodulated radiance, a: hit distance
Texture2D<float4> SpecularRadiance : register(t1);
Texture2D<float4> DiffuseDirection : register(t2); // xyz: incident dir (world), w: validity
Texture2D<float4> SpecularDirection : register(t3);
Texture2D<float4> MotionVectors : register(t4); // UV deltas (prevUV - currUV)
Texture2D<float> ViewDepth : register(t5); // low-res view-space depth
Texture2D<float4> NormalRoughnessLow : register(t6);

Texture2DArray<float4> DiffuseSHHistory : register(t7);
Texture2DArray<float4> SpecularSHHistory : register(t8);
Texture2D<float> HistoryDepth : register(t9);
Texture2D<float4> HistoryNormal : register(t10); // xyz: world normal, w: roughness

RWTexture2DArray<float4> DiffuseSHOut : register(u0);
RWTexture2DArray<float4> SpecularSHOut : register(u1);
RWTexture2D<float> DepthOut : register(u2);
RWTexture2D<float4> NormalOut : register(u3);

SamplerState LinearClampSampler : register(s0);
SamplerState PointClampSampler : register(s1);

static const float kDepthRejectThreshold = 0.05f;
static const float kNormalRejectDot = 0.95f;

void LoadHistorySH(Texture2DArray<float4> history, float2 huv, out float3 sh[9], out float extra)
{
    float4 slices[7];

    [unroll]
    for (uint s = 0; s < 7; s++)
        slices[s] = history.SampleLevel(LinearClampSampler, float3(huv, float(s)), 0);

    SHUnpack(slices, sh, extra);
}

void StoreSH(RWTexture2DArray<float4> output, uint2 idx, float3 sh[9], float extra)
{
    float4 slices[7];
    SHPack(sh, extra, slices);

    [unroll]
    for (uint s = 0; s < 7; s++)
        output[uint3(idx, s)] = slices[s];
}

bool IsValidSample(float4 radiance, float4 dir)
{
    return dir.w > 0.5f
        && !any(isnan(radiance.rgb)) && !any(isinf(radiance.rgb))
        && !any(isnan(dir.xyz)) && !any(isinf(dir.xyz));
}

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const float scale = Raytracing.ResolutionScale;
    const uint2 lowSize = (uint2)max(uint2(1, 1), (uint2)ceil(float2(Camera.RenderSize) * scale));

    if (any(idx >= lowSize))
        return;

    const float viewDepth = ViewDepth[idx];

    // 3x3 spatial prefilter of the current frame before temporal filtering.
    // A single directional sample rings badly in L2; averaging the projected
    // neighborhood suppresses negative lobes (which would decode to black).
    float3 currDiffSH[9];
    float3 currSpecSH[9];

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        currDiffSH[i] = 0.0f;
        currSpecSH[i] = 0.0f;
    }

    float diffHitSum = 0.0f;
    float specHitSum = 0.0f;
    float diffWeight = 0.0f;
    float specWeight = 0.0f;

    [unroll]
    for (int oy = -1; oy <= 1; oy++)
    {
        [unroll]
        for (int ox = -1; ox <= 1; ox++)
        {
            const int2 t = int2(idx) + int2(ox, oy);
            if (any(t < 0) || any(t >= int2(lowSize)))
                continue;

            const float4 diffRad = DiffuseRadiance[t];
            const float4 specRad = SpecularRadiance[t];
            const float4 diffDir = DiffuseDirection[t];
            const float4 specDir = SpecularDirection[t];

            if (IsValidSample(diffRad, diffDir))
            {
                SHAccumulate(normalize(diffDir.xyz), max(diffRad.rgb, 0.0f), currDiffSH);
                diffHitSum += diffRad.a;
                diffWeight += 1.0f;
            }

            if (IsValidSample(specRad, specDir))
            {
                SHAccumulate(normalize(specDir.xyz), max(specRad.rgb, 0.0f), currSpecSH);
                specHitSum += specRad.a;
                specWeight += 1.0f;
            }
        }
    }

    const bool diffValid = diffWeight > 0.0f;
    const bool specValid = specWeight > 0.0f;

    if (diffValid)
    {
        const float invW = 1.0f / diffWeight;

        [unroll]
        for (uint j = 0; j < 9; j++)
            currDiffSH[j] *= invW;

        diffHitSum *= invW;
    }

    if (specValid)
    {
        const float invW = 1.0f / specWeight;

        [unroll]
        for (uint k = 0; k < 9; k++)
            currSpecSH[k] *= invW;

        specHitSum *= invW;
    }

    // Reproject into history using normalized UV motion vectors (mv = prevUV - currUV).
    // History buffers are allocated at low-res size, so huv in [0, 1] spans the history.
    const float2 currUV = (float2(idx) + 0.5f) / float2(lowSize);
    const float2 mv = MotionVectors[idx].xy;
    const float2 huv = currUV + mv;
    const float2 prevLow = huv * float2(lowSize) - 0.5f;

    bool reset = PC.BlendAlpha >= 1.0f
        || any(!isfinite(mv))
        || any(huv < 0.0f)
        || any(huv >= 1.0f);

    float3 histDiffSH[9];
    float3 histSpecSH[9];
    float histExtra0, histExtra1;

    if (!reset)
    {
        LoadHistorySH(DiffuseSHHistory, huv, histDiffSH, histExtra0);
        LoadHistorySH(SpecularSHHistory, huv, histSpecSH, histExtra1);

        const float histDepth = HistoryDepth.SampleLevel(PointClampSampler, huv, 0);
        const float depthDenom = max(abs(viewDepth), 1e-3f);
        reset = abs(viewDepth - histDepth) / depthDenom > kDepthRejectThreshold;

        // World-space normals are view-independent, so this still validates
        // under camera rotation where depth legitimately changes.
        if (!reset)
        {
            const float4 currN = NormalRoughnessLow[idx];
            const float4 histN = HistoryNormal.SampleLevel(PointClampSampler, huv, 0);
            reset = dot(currN.xyz, histN.xyz) < kNormalRejectDot;
        }
    }

    if (reset)
    {
        [unroll]
        for (uint j = 0; j < 9; j++)
        {
            histDiffSH[j] = 0.0f;
            histSpecSH[j] = 0.0f;
        }
        histExtra0 = 0.0f;
        histExtra1 = 0.0f;
    }

    float3 outDiffSH[9];
    float3 outSpecSH[9];

    // Disocclusion snaps to the current sample; fast motion shortens the
    // history to avoid long smearing trails. Invalid current samples keep
    // history (or stay black when there is none yet).
    const float2 pixelMotion = mv * float2(Camera.RenderSize);
    const float motionA = saturate((length(pixelMotion) - 1.0f) / 8.0f);
    const float baseA = reset ? 1.0f : max(PC.BlendAlpha, motionA);
    const float diffA = diffValid ? baseA : 0.0f;
    const float specA = specValid ? baseA : 0.0f;

    [unroll]
    for (uint k = 0; k < 9; k++)
    {
        outDiffSH[k] = lerp(histDiffSH[k], currDiffSH[k], diffA);
        outSpecSH[k] = lerp(histSpecSH[k], currSpecSH[k], specA);
    }

    // Guard the history against poisoned samples.
    [unroll]
    for (uint m = 0; m < 9; m++)
    {
        if (any(isnan(outDiffSH[m])) || any(isinf(outDiffSH[m])))
            outDiffSH[m] = histDiffSH[m];
        if (any(isnan(outSpecSH[m])) || any(isinf(outSpecSH[m])))
            outSpecSH[m] = histSpecSH[m];
    }

    const float outExtra0 = lerp(histExtra0, diffHitSum, diffA);
    const float outExtra1 = lerp(histExtra1, specHitSum, specA);

    StoreSH(DiffuseSHOut, idx, outDiffSH, outExtra0);
    StoreSH(SpecularSHOut, idx, outSpecSH, outExtra1);
    DepthOut[idx] = viewDepth;
    NormalOut[idx] = NormalRoughnessLow[idx];
}
