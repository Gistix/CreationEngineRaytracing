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

RWTexture2D<float4> Output              : register(u0);

SamplerState LinearClampSampler         : register(s0);
SamplerState PointClampSampler          : register(s1);

#include "include/ColorConversions.hlsli"
#include "include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "include/PBR.hlsli"
#include "include/NRD.hlsli"

static const float kJBU_DepthSigma = 0.01f;

float4 SampleJBUTexel4(
    Texture2D<float4> texture,
    int2 base,
    float4 weights,
    int2 maxTexel)
{
    float4 result = 0;

    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        int2 texel = clamp(base + int2(i & 1, i >> 1), 0, maxTexel);
        result += texture.Load(int3(texel, 0)) * weights[i];
    }

    return result;
}

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

    float4 diffuseIndirect = DiffuseIndirect.SampleLevel(LinearClampSampler, giUV, 0);
    float4 specularIndirect = SpecularIndirect.SampleLevel(LinearClampSampler, giUV, 0);

    if (Raytracing.ResolutionScale < 1.0f) {
        // Joint bilateral upsampling: blend the 4 surrounding gi texels weighted by
        // depth (and optionally normal) similarity with the render-resolution surface.
        const int2 maxTexel = int2(ceil(float2(Camera.RenderSize) * Raytracing.ResolutionScale)) - 1;
        
        const float2 samplePos = giUV * float2(Camera.ScreenSize) - 0.5f;
        const int2 base = int2(floor(samplePos));

        const float2 f = frac(samplePos);

        float4 bilinearWeights =
        {
            (1.0f - f.x) * (1.0f - f.y),
            (       f.x) * (1.0f - f.y),
            (1.0f - f.x) * (       f.y),
            (       f.x) * (       f.y)
        };        

        const float depth0 = ScreenToViewDepth(FullResDepth[idx], Camera.CameraData);

        const float sigma = max(kJBU_DepthSigma * depth0, 1e-6f);
        
        float4 weights;
        float weightSum = 0.0f;
        
        [unroll]
        for (uint i = 0; i < 4; ++i) {
            const int2 texel = clamp(base + int2(i & 1, i >> 1), int2(0, 0), maxTexel);
            const float depth1 = LowResDepth[texel];
            
            float depthWeight = exp(-abs(depth0 - depth1) / sigma);
            
            weights[i] = depthWeight * bilinearWeights[i];

            weightSum += weights[i];
        }
        
        weights /= max(weightSum, 1e-6f);

        diffuseIndirect = SampleJBUTexel4(DiffuseIndirect, base, weights, maxTexel);
        specularIndirect = SampleJBUTexel4(SpecularIndirect, base, weights, maxTexel);
    }

#if defined(NRD_REBLUR)
    diffuseIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(diffuseIndirect);
    specularIndirect = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(specularIndirect);
#endif

    // Reconstruct material factors at render resolution, using the same G-buffer
    // inputs as NRD_MaterialFactors (albedo, metalness from GNMAO.y, roughness,
    // shading normal, and view direction).
    const float depth = FullResDepth.SampleLevel(PointClampSampler, screenUV, 0);
    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    const float3 positionVS = ScreenToViewPosition(screenUV, depthVS, Camera.NDCToView);
    const float3 viewDirection = normalize(-ViewToWorldPosition(positionVS, Camera.ViewInverse));

    const float4 normalRoughness = FullResNormalRoughness.SampleLevel(PointClampSampler, screenUV, 0);
    const float3 normal = normalRoughness.xyz;
    const float roughness = PBR::Roughness(saturate(normalRoughness.w), Raytracing.Roughness.x, Raytracing.Roughness.y);

    const float3 albedo = LLGammaToTrueLinear(Albedo.SampleLevel(PointClampSampler, screenUV, 0).rgb);
    const float metalness = Remap(saturate(GNMAO.SampleLevel(PointClampSampler, screenUV, 0).y), Raytracing.Metalness.x, Raytracing.Metalness.y);

    const float3 diffuseAlbedo = albedo * (1.0f - metalness);
    const float3 F0 = PBR::F0(albedo, metalness);

    float3 diffFactor, specFactor;
    NRD_MaterialFactors(normal, viewDirection, diffuseAlbedo, F0, roughness, diffFactor, specFactor);

    diffuseIndirect.rgb *= diffFactor;
    specularIndirect.rgb *= specFactor;
    
    Output[idx] = float4(diffuseIndirect.rgb + specularIndirect.rgb, 1.0f);
}
