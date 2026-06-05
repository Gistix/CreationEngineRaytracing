#include "interop/CameraData.hlsli"
#include "interop/SharedData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<FeatureData> Features    : register(b1);

Texture2D<float3> DiffuseAlbedo         : register(t0);
Texture2D<float4> DiffuseRadiance       : register(t1);
Texture2D<float4> SpecularRadiance      : register(t2);

#if defined(NRD)
Texture2D<float3> DiffuseFactor         : register(t3);
Texture2D<float3> SpecularFactor        : register(t4);
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
    
    float4 diffuseRadiance = DiffuseRadiance[idx];
    float4 specularRadiance = SpecularRadiance[idx];
    
#if defined(NRD)
    
#   if defined(NRD_REBLUR)
    diffuseRadiance = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuseRadiance);
    specularRadiance = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specularRadiance); 
#   endif
    
     diffuseRadiance *= float4(DiffuseFactor[idx], 1.0f);
     specularRadiance *= float4(SpecularFactor[idx], 1.0f);
#else   
    
    const float3 diffuseAlbedo = DiffuseAlbedo[idx];   
    diffuseRadiance *= float4(diffuseAlbedo, 1.0f);
#endif
 
    const float4 direct = Output[idx];    
    Output[idx] = float4(LLTrueLinearToGamma(direct.rgb + diffuseRadiance.rgb + specularRadiance.rgb), direct.a);
}