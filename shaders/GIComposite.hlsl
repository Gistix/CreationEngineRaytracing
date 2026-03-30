
#include "interop/CameraData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);

Texture2D<float4> Direct                : register(t0);
Texture2D<float4> Albedo                : register(t1);
Texture2D<float4> DiffuseIndirect       : register(t2);
Texture2D<float4> SpecularIndirect      : register(t3);

SamplerState DefaultSmapler             : register(s0);

RWTexture2D<float4> Output              : register(u0);

[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    uint2 size = Camera.RenderSize;
    
    if (any(idx >= size))
        return;
    
    float3 direct = Direct[idx].rgb;
    float3 albedo = Albedo[idx].rgb;
    float3 diffuseIndirect = DiffuseIndirect[idx].rgb;
    float3 specularIndirect = SpecularIndirect[idx].rgb;
    
    Output[idx] = float4(direct + diffuseIndirect * albedo + specularIndirect, 1.0f);
}