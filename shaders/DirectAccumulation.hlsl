#include "interop/CameraData.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);

Texture2D<float3> RawDirectDiffuse  : register(t0);
Texture2D<float3> RawDirectSpecular : register(t1);
Texture2D<float4> MotionVectors     : register(t2);
Texture2D<float>  ViewDepth         : register(t3);
Texture2D<float3> HistoryDiffuse    : register(t4);
Texture2D<float3> HistorySpecular   : register(t5);
Texture2D<float>  HistoryViewZ      : register(t6);

RWTexture2D<float3> OutAccumDiffuse  : register(u0);
RWTexture2D<float3> OutAccumSpecular : register(u1);
RWTexture2D<float>  OutAccumViewZ    : register(u2);

SamplerState LinearClampSampler : register(s0);

#include "include/WaveSize.hlsli"
#include "include/Vulkan.hlsli"

struct DirectAccumulationConstants
{
    uint ResetHistory;  // 1 = ignore history, output raw current frame
    float BlendFactor;  // weight of the current frame in the exponential blend
    float DepthThreshold; // relative view-depth tolerance for history validation
    uint _pad;
};

VK_PUSH_CONSTANT ConstantBuffer<DirectAccumulationConstants> PC : register(b1);

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 size = Camera.ScreenSize;

    if (any(idx >= size))
        return;

    const float2 uv = (float2(idx) + 0.5f) / float2(size);

    const float3 rawD = RawDirectDiffuse[idx];
    const float3 rawS = RawDirectSpecular[idx];
    const float viewZ = ViewDepth[idx];

    float3 outD = rawD;
    float3 outS = rawS;

    // Motion vectors are UV-space offsets (prev - current), jitter included.
    const float2 motion = MotionVectors[idx].xy;
    const float2 uvPrev = uv + motion;

    if (PC.ResetHistory == 0 && all(uvPrev >= 0.0f) && all(uvPrev < 1.0f)) {
        const float3 histD = HistoryDiffuse.SampleLevel(LinearClampSampler, uvPrev, 0);
        const float3 histS = HistorySpecular.SampleLevel(LinearClampSampler, uvPrev, 0);
        const float histVZ = HistoryViewZ.SampleLevel(LinearClampSampler, uvPrev, 0);

        const float relDepthDiff = abs(histVZ - viewZ) / max(viewZ, 1e-3f);

        if (relDepthDiff <= PC.DepthThreshold) {
            // Neighborhood clamp against the current frame to reject ghosts
            // (e.g. sweeping shadow edges on flat surfaces pass the depth test).
            float3 boxMinD = rawD, boxMaxD = rawD;
            float3 boxMinS = rawS, boxMaxS = rawS;

            [unroll]
            for (int y = -1; y <= 1; ++y) {
                [unroll]
                for (int x = -1; x <= 1; ++x) {
                    const int2 tap = int2(idx) + int2(x, y);
                    if (any(tap < 0) || any(tap >= int2(size)))
                        continue;
                    const float3 tapD = RawDirectDiffuse[tap];
                    const float3 tapS = RawDirectSpecular[tap];
                    boxMinD = min(boxMinD, tapD);
                    boxMaxD = max(boxMaxD, tapD);
                    boxMinS = min(boxMinS, tapS);
                    boxMaxS = max(boxMaxS, tapS);
                }
            }

            const float3 clampedD = clamp(histD, boxMinD, boxMaxD);
            const float3 clampedS = clamp(histS, boxMinS, boxMaxS);

            outD = lerp(clampedD, rawD, PC.BlendFactor);
            outS = lerp(clampedS, rawS, PC.BlendFactor);

            // Guard against NaN/Inf from degenerate samples
            if (any(isnan(outD)) || any(isinf(outD)))
                outD = rawD;
            if (any(isnan(outS)) || any(isinf(outS)))
                outS = rawS;
        }
    }

    OutAccumDiffuse[idx] = outD;
    OutAccumSpecular[idx] = outS;
    OutAccumViewZ[idx] = viewZ;
}
