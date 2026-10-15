#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<RaytracingData> Raytracing : register(b1);
ConstantBuffer<FeatureData> Features    : register(b2);

#include "include/Common.hlsli"
#include "include/WaveSize.hlsli"

SamplerState PointClampSampler          : register(s0);

Texture2D<float> DepthTexture           : register(t0);
Texture2D<float4> NormalRoughnessTexture : register(t1);
Texture2D<float4> PrimaryMotionVectors  : register(t2);

#if defined(DLSS_RR)
Texture2D<float4> AlbedoTexture         : register(t3);
Texture2D<float3> GNMAOTexture          : register(t4);
#endif

RWTexture2D<float4> OutNormalRoughness       : register(u0);
RWTexture2D<float4> OutMotionVectors         : register(u1);

#if defined(NRD)
Texture2D<float2> WaterNormalTexture     : register(t3);
Texture2D<float>  WaterDepthTexture      : register(t4);
RWTexture2D<float>  OutViewDepth         : register(u2);
#endif

#if defined(DLSS_RR)
#include "include/ColorConversions.hlsli"
#include "include/PBR.hlsli"
#include "include/Common/BRDF.hlsli"

RWTexture2D<float3> OutSpecularAlbedo        : register(u2);
#endif

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixelPos = dispatchThreadID.xy;

    // The pass runs at the scaled GI resolution.
    const float2 scaledSize = Camera.RenderSize * Raytracing.ResolutionScale;

    if (any(pixelPos >= scaledSize))
        return;
    
    // All inputs are screen-sized textures, with the engine G-buffer populated
    // only in the top-left RenderSize region. Map the scaled GI pixel back into
    // that region; normalizing by scaledSize would incorrectly stretch it across
    // the complete ScreenSize texture.
    const float2 sourceUV = (float2(pixelPos) + 0.5f) /
        (float2(Camera.ScreenSize) * Raytracing.ResolutionScale);

    const float4 normalRoughness = NormalRoughnessTexture.SampleLevel(PointClampSampler, sourceUV, 0);
    OutMotionVectors[pixelPos] = PrimaryMotionVectors.SampleLevel(PointClampSampler, sourceUV, 0);

#if defined(NRD) || defined(DLSS_RR)
    const float depth = DepthTexture.SampleLevel(PointClampSampler, sourceUV, 0);
    float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
#endif

#if defined(NRD)
    float4 finalNormalRoughness = normalRoughness;
    const float waterDepth = WaterDepthTexture.SampleLevel(PointClampSampler, sourceUV, 0);
    [branch]
    if (waterDepth < SKY_Z)
    {
        const float waterDepthVS = ScreenToViewDepth(waterDepth, Camera.CameraData);
        if (waterDepthVS >= FP_VIEW_Z && waterDepth <= depth + 1e-4f)
        {
            depthVS = waterDepthVS;
            const float2 waterNormalXY = WaterNormalTexture.SampleLevel(PointClampSampler, sourceUV, 0);
            const float3 waterNormalWS = float3(waterNormalXY, sqrt(saturate(1.0f - dot(waterNormalXY, waterNormalXY))));
            finalNormalRoughness = float4(waterNormalWS, 0.0f); // roughness = 0.0 for mirror water!
        }
    }
    OutNormalRoughness[pixelPos] = finalNormalRoughness;
    OutViewDepth[pixelPos] = depthVS;
#else
    OutNormalRoughness[pixelPos] = normalRoughness;
#endif

#if defined(DLSS_RR)
    const float roughness = PBR::Roughness(saturate(normalRoughness.w), Raytracing.Roughness.x, Raytracing.Roughness.y);
    const float3 normal = normalRoughness.xyz;

    const float3 positionVS = ScreenToViewPosition(sourceUV, depthVS, Camera.NDCToView);
    const float3 viewDirection = normalize(-ViewToWorldPosition(positionVS, Camera.ViewInverse));

    const float3 albedo = LLGammaToTrueLinear(AlbedoTexture.SampleLevel(PointClampSampler, sourceUV, 0).rgb);
    const float metalness = Remap(saturate(GNMAOTexture.SampleLevel(PointClampSampler, sourceUV, 0).y), Raytracing.Metalness.x, Raytracing.Metalness.y);

    const float3 F0 = PBR::F0(albedo, metalness);
    const float NdotV = abs(dot(normal, viewDirection));
    const float2 envBRDF = BRDF::EnvBRDF(roughness, NdotV);

    OutSpecularAlbedo[pixelPos] = float3(F0 * envBRDF.x + envBRDF.y);
#endif
}
