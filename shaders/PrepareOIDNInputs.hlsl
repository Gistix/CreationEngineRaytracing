#include "interop/CameraData.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);

Texture2D<float4>   InputColor   : register(t0);
Texture2D<float3>   InputAlbedo  : register(t1);
Texture2D<float3>   InputNormal  : register(t2);

RWBuffer<float4> OutputColor  : register(u0);
RWBuffer<float4> OutputAlbedo : register(u1);
RWBuffer<float4> OutputNormal : register(u2);

#include "include/WaveSize.hlsli"

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 id : SV_DispatchThreadID)
{
    if (any(id >= Camera.RenderSize))
        return;

    uint pixelIndex = id.y * Camera.RenderSize.x + id.x;

    // Color: full HDR radiance
    float4 color = InputColor[id];
    if (any(isnan(color.rgb)) || any(isinf(color.rgb)))
        color.rgb = float3(0.0f, 0.0f, 0.0f);
    OutputColor[pixelIndex] = color;

    // Albedo: raw diffuse albedo in [0, 1]
    float3 albedo = InputAlbedo[id];
    if (any(isnan(albedo)) || any(isinf(albedo)))
        albedo = float3(0.0f, 0.0f, 0.0f);
    OutputAlbedo[pixelIndex] = float4(saturate(albedo), 1.0f);

    // Normal: decoded from FaceNormals ([0, 1] -> [-1, 1])
    float3 normalEnc = InputNormal[id];
    float3 normal = normalEnc * 2.0f - 1.0f;
    float lenSq = dot(normal, normal);
    if (lenSq > 1e-4f && !isnan(lenSq) && !isinf(lenSq))
        normal = normalize(normal);
    else
        normal = float3(0.0f, 0.0f, 1.0f);
    OutputNormal[pixelIndex] = float4(normal, 0.0f);
}
