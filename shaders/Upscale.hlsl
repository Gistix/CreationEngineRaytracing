#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<RaytracingData> Raytracing : register(b1);

Texture2D<float4> InputTexture          : register(t0);
RWTexture2D<float4> OutputTexture       : register(u0);

SamplerState LinearClampSampler         : register(s0);

[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 size = Camera.ScreenSize;
    
    if (any(idx >= size))
        return;
    
    const float2 uv = (float2(idx) + 0.5f) / size;
    const float2 inputUV = uv  * Raytracing.ResolutionScale;

    OutputTexture[idx] = InputTexture.SampleLevel(LinearClampSampler, inputUV, 0);
}
