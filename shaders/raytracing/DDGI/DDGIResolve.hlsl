#include "interop/CameraData.hlsli"
#include "interop/DDGIData.hlsli"

ConstantBuffer<CameraData>      Camera               : register(b0);
ConstantBuffer<DDGIData>        DDGI                 : register(b1);

#include "include/Common.hlsli"
#include "include/WaveSize.hlsli"

#include "ddgi/include/ProbeCommon.hlsl"
#include "ddgi/Irradiance.hlsl"

Texture2D<float>                Depth                : register(t0);
Texture2D<snorm float4>         NormalRoughness      : register(t1);

Texture2DArray<float4>          ProbeIrradiance      : register(t2);
Texture2DArray<float4>          ProbeDistance        : register(t3);
Texture2DArray<float4>          ProbeData            : register(t4);

RWTexture2D<float4>             OutputDiffuse        : register(u0);

SamplerState                    DefaultSampler       : register(s0);
SamplerState                    BilinearClampSampler : register(s1);

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    if (any(idx >= Camera.RenderSize))
        return;

    const float2 uv = float2(idx + 0.5f) / Camera.RenderSize;
    const float depth = Depth.SampleLevel(DefaultSampler, uv, 0);

    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    if (depthVS < FP_VIEW_Z || depth >= SKY_Z)
    {
        OutputDiffuse[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 worldPos = GetWorldPosition(idx, depth);
    float3 normal = NormalRoughness.SampleLevel(DefaultSampler, uv, 0).xyz;

    float normalLen = length(normal);
    if (normalLen < 1e-3f)
    {
        OutputDiffuse[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    normal /= normalLen;

    float3 viewDir = normalize(Camera.Position.xyz - worldPos);

    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(DDGI.Volume);

    DDGIVolumeResources resources;
    resources.probeIrradiance = ProbeIrradiance;
    resources.probeDistance = ProbeDistance;
    resources.probeData = ProbeData;
    resources.bilinearSampler = BilinearClampSampler;

    float3 surfaceBias = DDGIGetSurfaceBias(normal, viewDir, volume);
    float3 irradiance = DDGIGetVolumeIrradiance(worldPos, surfaceBias, normal, volume, resources);

    OutputDiffuse[idx] = float4(irradiance, 1.0f);
}
