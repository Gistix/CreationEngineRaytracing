#include "interop/CameraData.hlsli"
#include "interop/SharedData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<FeatureData> Features    : register(b1);
Buffer<float4> DenoisedLinear          : register(t0);
RWTexture2D<float4> MainTexture         : register(u0);

#include "include/ColorConversions.hlsli"
#include "include/WaveSize.hlsli"

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 id : SV_DispatchThreadID)
{
    if (any(id >= Camera.RenderSize))
        return;

    uint pixelIndex = id.y * Camera.RenderSize.x + id.x;
    float4 color = DenoisedLinear[pixelIndex];
    MainTexture[id] = float4(LLTrueLinearToGamma(color.rgb), color.a);
}
