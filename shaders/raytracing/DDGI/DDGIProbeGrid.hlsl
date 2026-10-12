#include "interop/CameraData.hlsli"
#include "interop/DDGIData.hlsli"
#include "ddgi/include/ProbeCommon.hlsl"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<DDGIData> DDGI : register(b1);
Texture2DArray<float4> ProbeData : register(t0);

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 LocalPosition : TEXCOORD0;
    nointerpolation float Active : TEXCOORD1;
};

VertexOutput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
        float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(1.0f, -1.0f)
    };

    const DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(DDGI.Volume);
    const int3 probeCoords = DDGIGetProbeCoords(instanceId, volume);
    const int storageIndex = DDGIGetScrollingProbeIndex(probeCoords, volume);
    const uint3 texelCoords = DDGIGetProbeTexelCoords(storageIndex, volume);
    const float4 probeData = ProbeData.Load(int4(texelCoords, 0));
    const float3 probePosition = DDGIGetProbeWorldPosition(probeCoords, volume, ProbeData);
    const float4 centerClip = mul(Camera.ViewProj, float4(probePosition - Camera.Position, 1.0f));

    VertexOutput output;
    const float2 corner = corners[vertexId];
    output.LocalPosition = corner;
    output.Active = probeData.w == 0.0f ? 1.0f : 0.0f;

    if (centerClip.w <= 0.0f)
    {
        output.Position = float4(2.0f, 2.0f, 1.0f, 1.0f);
        return output;
    }

    const float worldRadius = min(volume.probeSpacing.x, min(volume.probeSpacing.y, volume.probeSpacing.z)) * 0.08f;
    const float projectedRadius = worldRadius * abs(Camera.ViewProj[1][1]) * Camera.ScreenSize.y * 0.5f / centerClip.w;
    const float radiusPixels = clamp(projectedRadius, 2.5f, 12.0f);
    float4 clipPosition = centerClip;
    clipPosition.xy += corner * (radiusPixels * 2.0f / Camera.ScreenSize) * centerClip.w;
    output.Position = clipPosition;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    const float radiusSquared = dot(input.LocalPosition, input.LocalPosition);
    clip(1.0f - radiusSquared);

    const float3 normal = normalize(float3(input.LocalPosition, sqrt(max(1.0f - radiusSquared, 0.0f))));
    const float lighting = 0.3f + 0.7f * saturate(dot(normal, normalize(float3(-0.4f, 0.6f, 1.0f))));
    const float3 probeColor = input.Active > 0.5f ? float3(0.1f, 0.85f, 0.3f) : float3(0.95f, 0.12f, 0.06f);
    const float rim = smoothstep(0.55f, 1.0f, radiusSquared);
    return float4(lerp(probeColor * lighting, float3(1.0f, 0.9f, 0.35f), rim * 0.35f), 1.0f);
}
