#include "interop/CameraData.hlsli"
#include "interop/SharedData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<FeatureData> Features    : register(b1);

Texture2D<float4> Albedo                : register(t0);
Texture2D<unorm float4> GNMAO           : register(t1);
Texture2D<float4> DiffuseIndirect       : register(t2);
Texture2D<float4> SpecularIndirect      : register(t3);

#if defined(NRD)
Texture2D<float3> DiffuseFactor         : register(t4);
Texture2D<float3> SpecularFactor        : register(t5);
#endif

RWTexture2D<float4> Output              : register(u0);

#include "include/ColorConversions.hlsli"

#if defined(NRD_REBLUR)
#include "include/NRD.hlsli"
#endif

[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 size = Camera.ScreenSize;
    
    if (any(idx >= size))
        return;
    
    float4 diffuseIndirect = DiffuseIndirect[idx];
    float4 specularIndirect = SpecularIndirect[idx];

#if defined(NRD)
    
#   if defined(NRD_REBLUR)
    diffuseIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuseIndirect);
    specularIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specularIndirect); 
#   endif
    
     diffuseIndirect *= float4(DiffuseFactor[idx], 1.0f);
     specularIndirect *= float4(SpecularFactor[idx], 1.0f);
#else   
    const float3 albedo = LLGammaToTrueLinear(Albedo[idx].rgb);
    const float metalness = GNMAO[idx].z;
    
    const float3 diffuseAlbedo = albedo * (1.0f - metalness);
    
    diffuseIndirect *= float4(diffuseAlbedo, 1.0f);
#endif
    
    Output[idx] = float4(diffuseIndirect.rgb + specularIndirect.rgb, 1.0f);
}