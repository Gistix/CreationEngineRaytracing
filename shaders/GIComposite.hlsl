#include "interop/CameraData.hlsli"
#include "interop/SharedData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<FeatureData> Features    : register(b1);

Texture2D<float4> Albedo                : register(t0);
Texture2D<float4> DiffuseIndirect       : register(t1);
Texture2D<float4> SpecularIndirect      : register(t2);

RWTexture2D<float4> Output              : register(u0);

#include "include/ColorConversions.hlsli"

#if defined(NRD_REBLUR)
#include "include/NRD.hlsli"
#endif

[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    /*const uint2 size = Camera.ScreenSize;
    
    if (any(idx >= size))
        return;*/
    
    float3 albedo = Albedo[idx].rgb;
    float4 diffuseIndirect = DiffuseIndirect[idx];
    float4 specularIndirect = SpecularIndirect[idx];
    
#if defined(NRD_REBLUR)
    diffuseIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuseIndirect);
    specularIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specularIndirect);   
#endif
    
    Output[idx] = float4(diffuseIndirect.rgb * albedo + specularIndirect.rgb, 1.0f);
}