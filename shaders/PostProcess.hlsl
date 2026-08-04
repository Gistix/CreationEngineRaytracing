#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<RaytracingData> Raytracing : register(b1);

#include "include/Common.hlsli"

SamplerState PointClampSampler          : register(s0);

Texture2D<float> DepthTexture           : register(t0);
Texture2D<float4> NormalRoughnessTexture : register(t1);
Texture2D<float4> PrimaryMotionVectors  : register(t2);

RWTexture2D<float4> OutNormalRoughness       : register(u0);
RWTexture2D<float2> OutMotionVectors         : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixelPos = dispatchThreadID.xy;

    // The pass runs at the scaled GI resolution.
    const float2 scaledSize = Camera.RenderSize * Raytracing.ResolutionScale;

    if (any(pixelPos >= scaledSize))
        return;
    
    // All inputs are screen-sized textures, with the engine G-buffer populated
    // only in the top-left RenderSize region. Map the scaled GI pixel back into
    // that region; normalizing by scaledSize would incorrectly stretch it across
    // the complete ScreenSize texture.
    const float2 sourceUV = (float2(pixelPos) + 0.5f) /
        (float2(Camera.ScreenSize) * Raytracing.ResolutionScale);
    
    // Point-sample the G-buffer and motion vectors at the corresponding GI pixel.
    OutNormalRoughness[pixelPos] = NormalRoughnessTexture.SampleLevel(PointClampSampler, sourceUV, 0);
    OutMotionVectors[pixelPos] = PrimaryMotionVectors.SampleLevel(PointClampSampler, sourceUV, 0).xy;
}
