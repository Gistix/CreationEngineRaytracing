#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"

ConstantBuffer<CameraData> Camera       : register(b0);
ConstantBuffer<FeatureData> Features    : register(b1);
ConstantBuffer<RaytracingData> Raytracing : register(b2);

Texture2D<float4> DiffuseIndirect       : register(t0);
Texture2D<float4> SpecularIndirect      : register(t1);
Texture2D<float> LowResDepth            : register(t2);
Texture2D<float> FullResDepth           : register(t3);
Texture2D<float3> Albedo                : register(t4);
Texture2D<float4> FullResNormalRoughness : register(t5);
Texture2D<float3> GNMAO                 : register(t6);
Texture2D<float2> WaterNormal           : register(t7);
Texture2D<float>  WaterDepth            : register(t8);

RWTexture2D<float4> Output              : register(u0);

SamplerState LinearClampSampler         : register(s0);
SamplerState PointClampSampler          : register(s1);

#include "include/ColorConversions.hlsli"
#include "include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "include/PBR.hlsli"
#include "include/NRD.hlsli"

static const float kJBU_DepthSigma = 0.01f;

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 size = Camera.RenderSize;
    
    if (any(idx >= size))
        return;
    
    // Every texture is allocated at ScreenSize. The engine populates the G-buffer
    // only in its top-left RenderSize region, so keep all G-buffer UVs in the
    // screen-sized backing-texture coordinate space.
    const float2 screenUV = (float2(idx) + 0.5f) / float2(Camera.ScreenSize);

    // GI occupies the top-left ceil(RenderSize * ResolutionScale) region of its
    // screen-sized textures. Scaling screenUV maps each render pixel to that
    // populated GI region without changing the RenderSize output coverage.
    const float2 giUV = screenUV * Raytracing.ResolutionScale;

    // Reconstruct material factors and geometric attributes at render resolution.
    const float depth = FullResDepth.SampleLevel(PointClampSampler, screenUV, 0);
    float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    float3 positionVS = ScreenToViewPosition(screenUV, depthVS, Camera.NDCToView);
    float3 viewDirection = normalize(-ViewToWorldPosition(positionVS, Camera.ViewInverse));

    const float4 normalRoughness = FullResNormalRoughness.SampleLevel(PointClampSampler, screenUV, 0);
    float3 normal = normalRoughness.xyz;
    const float roughness = PBR::Roughness(saturate(normalRoughness.w), Raytracing.Roughness.x, Raytracing.Roughness.y);

    const float3 albedo = LLGammaToTrueLinear(Albedo.SampleLevel(PointClampSampler, screenUV, 0).rgb);
    const float metalness = Remap(saturate(GNMAO.SampleLevel(PointClampSampler, screenUV, 0).y), Raytracing.Metalness.x, Raytracing.Metalness.y);

    const float3 diffuseAlbedo = albedo * (1.0f - metalness);
    const float3 F0 = PBR::F0(albedo, metalness);

    float3 diffFactor, specFactor;
    NRD_MaterialFactors(normal, viewDirection, diffuseAlbedo, F0, roughness, diffFactor, specFactor);

    // Resolve water surface depth and normal if present so high-res properties
    // match the low-res properties generated in PostProcess.hlsl.
    const float waterDepth = WaterDepth.SampleLevel(PointClampSampler, screenUV, 0);
    bool isWater = false;
    float waterFresnel = 0.0f;

    [branch]
    if (waterDepth < SKY_Z)
    {
        const float waterDepthVS = ScreenToViewDepth(waterDepth, Camera.CameraData);
        if (waterDepthVS >= FP_VIEW_Z && waterDepth <= depth + 1e-4f)
        {
            isWater = true;
            depthVS = waterDepthVS;
            const float2 waterNormalXY = WaterNormal.SampleLevel(PointClampSampler, screenUV, 0);
            const float3 waterNormalWS = float3(waterNormalXY, sqrt(saturate(1.0f - dot(waterNormalXY, waterNormalXY))));
            normal = waterNormalWS;
            positionVS = ScreenToViewPosition(screenUV, depthVS, Camera.NDCToView);
            viewDirection = normalize(-ViewToWorldPosition(positionVS, Camera.ViewInverse));

            float3 unusedDiff;
            NRD_MaterialFactors(normal, viewDirection, float3(0.0f, 0.0f, 0.0f), float3(0.02f, 0.02f, 0.02f), 0.0f, unusedDiff, specFactor);

            float NdotV = saturate(dot(normal, viewDirection));
            waterFresnel = 0.02f + (1.0f - 0.02f) * pow(saturate(1.0f - NdotV), 5.0f);
        }
    }

    const float3 pos0WS = ViewToWorldPosition(positionVS, Camera.ViewInverse);

    float4 diffuseIndirect;
    float4 specularIndirect;

    if (Raytracing.ResolutionScale < 1.0f) {
        // Joint bilateral upsampling: 3x3 footprint with plane-aware depth weighting,
        // normal similarity, and a nearest-matching fallback for fine silhouettes.
        const int2 maxTexel = int2(ceil(float2(Camera.RenderSize) * Raytracing.ResolutionScale)) - 1;
        const float2 samplePos = giUV * float2(Camera.ScreenSize) - 0.5f;
        const int2 centerTexel = int2(round(samplePos));

        const float NdotV = saturate(dot(normal, viewDirection));
        const float planeTolerance = max(kJBU_DepthSigma * depthVS / max(NdotV, 0.25f), 0.5f);

        float4 diffAccum = 0.0f;
        float4 specAccum = 0.0f;
        float weightSum = 0.0f;

        float maxWeight = -1.0f;
        int2 bestTexel = centerTexel;

        [unroll]
        for (int dy = -1; dy <= 1; ++dy)
        {
            [unroll]
            for (int dx = -1; dx <= 1; ++dx)
            {
                int2 texel = clamp(centerTexel + int2(dx, dy), 0, maxTexel);
                float depth1 = LowResDepth[texel];

                // 1. World position of low-res depth sample
                float2 sampleUV = (float2(texel) + 0.5f) / (float2(Camera.ScreenSize) * Raytracing.ResolutionScale);
                float3 samplePosVS = ScreenToViewPosition(sampleUV, depth1, Camera.NDCToView);
                float3 samplePosWS = ViewToWorldPosition(samplePosVS, Camera.ViewInverse);

                // 2. Point-to-plane distance (invariant to surface slope/foreshortening)
                float planeDist = abs(dot(samplePosWS - pos0WS, normal));

                // 3. Normal similarity (preserves geometric creases/corners while permitting normal maps)
                float3 normal1 = FullResNormalRoughness.SampleLevel(PointClampSampler, sampleUV, 0).xyz;
                [branch]
                if (isWater)
                {
                    float waterDepth1 = WaterDepth.SampleLevel(PointClampSampler, sampleUV, 0);
                    if (waterDepth1 < SKY_Z)
                    {
                        float2 waterNorm1 = WaterNormal.SampleLevel(PointClampSampler, sampleUV, 0);
                        normal1 = float3(waterNorm1, sqrt(saturate(1.0f - dot(waterNorm1, waterNorm1))));
                    }
                }

                float normalSim = saturate(dot(normal, normal1));
                float normalWeight = pow(normalSim, 16.0f);

                // 4. Spatial Gaussian weight
                float2 offset = float2(dx, dy) - (samplePos - float2(centerTexel));
                float spatialWeight = exp(-dot(offset, offset) * 1.5f);

                // 5. Plane-aware depth weight
                float depthWeight = exp(-planeDist / planeTolerance);

                float w = spatialWeight * depthWeight * normalWeight;

                if (w > maxWeight)
                {
                    maxWeight = w;
                    bestTexel = texel;
                }

                diffAccum += DiffuseIndirect.Load(int3(texel, 0)) * w;
                specAccum += SpecularIndirect.Load(int3(texel, 0)) * w;
                weightSum += w;
            }
        }

        if (weightSum > 1e-3f)
        {
            diffuseIndirect = diffAccum / weightSum;
            specularIndirect = specAccum / weightSum;
        }
        else
        {
            // Fallback for fine geometry: select the best matching sample on the plane
            diffuseIndirect = DiffuseIndirect.Load(int3(bestTexel, 0));
            specularIndirect = SpecularIndirect.Load(int3(bestTexel, 0));
        }
    }
    else
    {
        diffuseIndirect = DiffuseIndirect.SampleLevel(LinearClampSampler, giUV, 0);
        specularIndirect = SpecularIndirect.SampleLevel(LinearClampSampler, giUV, 0);
    }

#if defined(NRD_REBLUR)
    diffuseIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuseIndirect);
    specularIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specularIndirect);
#endif

    diffuseIndirect.rgb *= diffFactor;
    if (isWater)
    {
        diffuseIndirect.rgb *= (1.0f - waterFresnel);
        specularIndirect.rgb *= specFactor;
    }
    else
    {
        specularIndirect.rgb *= specFactor;
    }
    
    Output[idx] = float4(diffuseIndirect.rgb + specularIndirect.rgb, 1.0f);
}
