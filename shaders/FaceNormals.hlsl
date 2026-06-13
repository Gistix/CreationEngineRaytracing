#include "interop/CameraData.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);

Texture2D<float>    Depth         : register(t0);
RWTexture2D<float3> FaceNormals   : register(u0);

#include "include/Common.hlsli"

[numthreads(8, 8, 1)]
void Main(uint2 id : SV_DispatchThreadID)
{
    if (any(id >= Camera.RenderSize)) 
        return;
    
    // If the game has dynamic resolution enabled the textures will not cover the entire extent
    const float2 dynamicUV = float2(id + 0.5f) / Camera.ScreenSize;
    
    const float2 dynamicUVUnjittered = dynamicUV - (Camera.Jitter / Camera.ScreenSize);
    
    const int2 depthID = dynamicUVUnjittered * Camera.ScreenSize;
    
    FaceNormals[id] = ComputeNormalImproved(Depth, id, depthID) * 0.5f + 0.5f;
}