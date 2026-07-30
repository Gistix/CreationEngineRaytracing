#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);

#include "include/Common.hlsli"

Texture2D<float> DepthTexture : register(t0);
Texture2D<float4> NormalRoughnessTexture : register(t1);
Texture2D<float4> PrimaryMotionVectors : register(t2);
Texture2D<float> SpecularHitDistanceTexture : register(t3);

RWTexture2D<float2> OutSpecularMotionVectors : register(u0);

static const float kSpecularRoughnessThreshold = 0.35f;

float2 ComputeSpecularMotionVector(
    float3 primaryHitPosWorld,
    float3 primaryRayDirWorld,
    float3 primaryHitNormalWorld,
    float3 reflectionRayWorld,
    float4x4 worldToClipMatrix,
    float4x4 prevWorldToClipMatrix)
{
    float3 principalAxis = primaryHitNormalWorld;
    float zObj = dot(principalAxis, reflectionRayWorld);
    if (zObj < 0.0f) {
        principalAxis = -principalAxis;
        zObj = -zObj;
    }
    zObj = max(zObj, 1e-5f);

    float3 n = principalAxis;
    float s = n.z < 0.0f ? -1.0f : 1.0f;
    float a = -1.0f / (s + n.z);
    float b = n.x * n.y * a;
    float3 xAxis = float3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
    float3 yAxis = float3(b, s + n.y * n.y * a, -n.y);

    float xObj = dot(xAxis, reflectionRayWorld);
    float yObj = dot(yAxis, reflectionRayWorld);
    float3 imagePosInReflectorSpace = float3(xObj, yObj, -zObj);

    float3 imageInWorld = primaryHitPosWorld + primaryRayDirWorld * length(imagePosInReflectorSpace);

    float3 imageCameraRelative = imageInWorld - Camera.Position.xyz;
    float4 prev_clip_pos = mul(prevWorldToClipMatrix, float4(imageCameraRelative, 1.0f));
    float2 prev_ndc = prev_clip_pos.xy / prev_clip_pos.w;

    float3 currCameraRelative = primaryHitPosWorld - Camera.Position.xyz;
    float4 curr_clip_pos = mul(worldToClipMatrix, float4(currCameraRelative, 1.0f));
    float2 curr_ndc = curr_clip_pos.xy / curr_clip_pos.w;

    float2 specularVelocity = prev_ndc - curr_ndc;
    return specularVelocity * float2(0.5f, -0.5f);
}

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
    float specHitT = SpecularHitDistanceTexture[pixelPos];
    float2 primaryMV = PrimaryMotionVectors[pixelPos].xy;

    float2 specMotionVector = primaryMV;

    if (depth < 1.0f && specHitT > 1e-3f && roughness < kSpecularRoughnessThreshold)
    {
        float3 posWS = GetWorldPosition(pixelPos, depth);
        float3 viewDirWS = normalize(Camera.Position.xyz - posWS);
        float3 reflectionRayWorld = reflect(-viewDirWS, normal) * specHitT;

        specMotionVector = ComputeSpecularMotionVector(
            posWS,
            -viewDirWS,
            normal,
            reflectionRayWorld,
            Camera.ViewProj,
            Camera.PrevViewProj
        );
    }

    OutSpecularMotionVectors[pixelPos] = specMotionVector;
}
