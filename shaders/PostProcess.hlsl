#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<RaytracingData> Raytracing : register(b1);

#include "include/Common.hlsli"

SamplerState PointClampSampler          : register(s0);

Texture2D<float> DepthTexture           : register(t0);
Texture2D<float4> NormalRoughnessTexture : register(t1);
Texture2D<float4> PrimaryMotionVectors  : register(t2);
Texture2D<float4> SpecularHitDistanceTexture : register(t3);

RWTexture2D<float2> OutSpecularMotionVectors : register(u0);
RWTexture2D<float4> OutNormalRoughness       : register(u1);
RWTexture2D<float4> OutMotionVectors         : register(u2);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixelPos = dispatchThreadID.xy;

    // The pass runs at the scaled (gi) resolution. Normalize against the scaled
    // size so point-sampling the full-res inputs reads the texel for the pixel
    // that the scaled frame actually represents (rect pixel (x, y) == screen (x/scale, y/scale)).
    const float2 scaledSize = Camera.RenderSize * Raytracing.ResolutionScale;

    if (any(pixelPos >= scaledSize))
        return;

    const float2 uv = (float2(pixelPos) + 0.5f) / scaledSize;

    const float2 primaryMV = PrimaryMotionVectors.SampleLevel(PointClampSampler, uv, 0).xy;
    
#if WRITE_DOWNSCALED_NRD_INPUTS
    // Point-sample NormalRoughness and MotionVectors for NRD at giResolution
    const float4 normRough = NormalRoughnessTexture.SampleLevel(PointClampSampler, uv, 0);

    OutNormalRoughness[pixelPos] = normRough;
    OutMotionVectors[pixelPos] = float4(primaryMV, 0, 0);
#endif

#if GENERATE_SPECULAR_MOTION_VECTORS
    // Specular motion vectors for DLSS RR
    const float depth = DepthTexture.SampleLevel(PointClampSampler, uv, 0);

    float4 specHitTDir = SpecularHitDistanceTexture.SampleLevel(PointClampSampler, uv, 0);
    const float specHitT = specHitTDir.x;

    float2 specMotionVector = primaryMV;
    if (depth < 1.0f && specHitT > 1e-3f)
    {
        const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
        const float3 positionVS = ScreenToViewPosition(uv, depthVS, Camera.NDCToView);
        const float3 posWS = ViewToWorldPosition(positionVS, Camera.ViewInverse) + Camera.Position.xyz;

        float3 specDir = specHitTDir.yzw;

        float3 imageInWorld = posWS + specDir * specHitT;
        specMotionVector = compute2DMotionVector(imageInWorld, imageInWorld);
    }

    OutSpecularMotionVectors[pixelPos] = specMotionVector;
#endif
}
