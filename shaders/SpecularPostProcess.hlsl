#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);

#include "include/Common.hlsli"

Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> NormalRoughnessTexture : register(t1);
Texture2D<float4> PrimaryMotionVectors : register(t2);
Texture2D<float4> SpecularHitDistanceTexture : register(t3);

RWTexture2D<float2> OutSpecularMotionVectors : register(u0);

static const float kSpecularRoughnessThreshold = 0.35f;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadID.xy;
    if (any(pixelPos >= Camera.RenderSize))
        return;

    float depth = DepthTexture[pixelPos];
    float4 normRough = NormalRoughnessTexture[pixelPos];
    float3 normal = normRough.xyz;
    float roughness = normRough.w;
    float4 specHitTDir = SpecularHitDistanceTexture[pixelPos];
    
    float specHitT = specHitTDir.x;

    float2 primaryMV = PrimaryMotionVectors[pixelPos].xy;

    float2 specMotionVector = primaryMV;

    if (depth < 1.0f && specHitT > 1e-3f)
    {
        float3 posWS = GetWorldPosition(pixelPos, depth);
        float3 viewDirWS = normalize(Camera.Position.xyz - posWS);

        float3 specDir = specHitTDir.yzw;             
        
        float3 imageInWorld = posWS + specDir * specHitT;
        specMotionVector = compute2DMotionVector(imageInWorld, imageInWorld);
    }

    OutSpecularMotionVectors[pixelPos] = specMotionVector;
}
