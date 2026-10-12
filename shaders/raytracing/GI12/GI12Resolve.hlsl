// GI-1.2 resolve: per-pixel compute gather from the two-level cache.
// Capsaicin equivalent: interpolate_screen_probes + hash-grid resolve +
// gi_denoiser front-end packing. No ray-tracing pipeline: primary visibility
// comes from the raster GBuffer, diffuse irradiance from L1 screen-probe SH,
// specular/indirect from the L2 hash grid. Outputs match the ray-traced GI
// pass (demodulated RAW_RADIANCE for NRD, combined radiance otherwise) so
// NRDIntegration and GIComposite consume them unchanged.

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/GI12Data.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<GI12Data> GI12Params : register(b1);
ConstantBuffer<GI12ScreenProbesData> ScreenProbes : register(b2);
ConstantBuffer<GI12HashGridData> HashGrid : register(b3);
ConstantBuffer<RaytracingData> Raytracing : register(b4);
ConstantBuffer<FeatureData> Features : register(b5);

Texture2D<float> FullResDepth : register(t0);
Texture2D<float3> Albedo : register(t1);
Texture2D<float4> FullResNormalRoughness : register(t2);
Texture2D<float3> GNMAO : register(t3);
StructuredBuffer<uint4> ProbeSH : register(t4);
StructuredBuffer<uint2> Value : register(t5);
StructuredBuffer<uint2> ValueIndirect : register(t6);
StructuredBuffer<uint> Hash : register(t7);
StructuredBuffer<float4> ProbePosition : register(t8);
StructuredBuffer<float4> ProbeNormal : register(t9);
RaytracingAccelerationStructure Scene : register(t10);
StructuredBuffer<float4> ProbeRadiance : register(t11);
Texture2D<float4> SkyHemisphere : register(t12);
Texture2D<float3> FaceNormals : register(t13);

#if defined(RAW_RADIANCE) && defined(NRD)
RWTexture2D<float4> DiffuseOutput : register(u0);
RWTexture2D<float4> SpecularOutput : register(u1);
#else
RWTexture2D<float4> Output : register(u0);
#endif

SamplerState LinearClampSampler : register(s0);
SamplerState PointClampSampler : register(s1);
SamplerState LinearWrapSampler : register(s2);

#include "include/ColorConversions.hlsli"
#include "include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "include/PBR.hlsli"
#include "raytracing/include/GI12Cache.hlsli"

float3 GI12_ResolveSky(float3 direction)
{
    float3 d = direction;
    d.z = max(d.z, 0.0f);
    const float r = sqrt(max(1.0f - d.z, 0.0f));
    const float phi = atan2(d.y, d.x);
    const float2 uv = float2(cos(phi), sin(phi)) * r * 0.5f + 0.5f;
    return LLGammaToTrueLinear(SkyHemisphere.SampleLevel(LinearWrapSampler, uv, 0).rgb) * Raytracing.Sky;
}

#if defined(NRD)
#include "include/NRD.hlsli"
#endif

// Sample-count at which a cache entry is considered converged (matches the
// L2 lookup gate and the L1 per-frame accumulation scale).
static const float GI12_RESOLVE_CONVERGED_SAMPLES = 16.0f;

WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    const uint2 size = Camera.RenderSize;

    if (any(idx >= size))
        return;

    const float2 screenUV = GI12_UnjitterUV((float2(idx) + 0.5f) / float2(Camera.ScreenSize),
        Camera.Jitter, Camera.ScreenSize);

#if defined(RAW_RADIANCE) && defined(NRD)
    #if defined(NRD_REBLUR)
        #define GI12_PACK_DIFFUSE(r, h) REBLUR_FrontEnd_PackRadianceAndNormHitDist(r, h, true)
        #define GI12_PACK_SPECULAR(r, h) REBLUR_FrontEnd_PackRadianceAndNormHitDist(r, h, true)
    #else
        #define GI12_PACK_DIFFUSE(r, h) RELAX_FrontEnd_PackRadianceAndHitDist(r, h, true)
        #define GI12_PACK_SPECULAR(r, h) RELAX_FrontEnd_PackRadianceAndHitDist(r, h, true)
    #endif

    // Miss/sky: same convention as the ray-traced GI pass.
    #define GI12_WRITE_SKY() \
        { \
            DiffuseOutput[idx] = GI12_PACK_DIFFUSE(0.0f, 0.0f); \
            SpecularOutput[idx] = GI12_PACK_SPECULAR(0.0f, 0.0f); \
        }
#else
    #define GI12_WRITE_SKY() { Output[idx] = float4(0.0f, 0.0f, 0.0f, 0.0f); }
#endif

    const float depth = FullResDepth.SampleLevel(PointClampSampler, screenUV, 0);
    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);

    if (depthVS < FP_VIEW_Z || depth >= SKY_Z) {
        GI12_WRITE_SKY();
        return;
    }

    const float3 positionVS = ScreenToViewPosition(screenUV, depthVS, Camera.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Camera.ViewInverse);
    const float3 positionWS = positionCS + Camera.Position.xyz;

    const float4 normalRoughness = FullResNormalRoughness.SampleLevel(PointClampSampler, screenUV, 0);
    const float3 normal = normalize(normalRoughness.xyz);
    const float3 faceNormal = normalize(FaceNormals.SampleLevel(PointClampSampler, screenUV, 0) * 2.0f - 1.0f);
    const float roughness = PBR::Roughness(saturate(normalRoughness.w), Raytracing.Roughness.x, Raytracing.Roughness.y);

    const float3 albedo = LLGammaToTrueLinear(Albedo.SampleLevel(PointClampSampler, screenUV, 0).rgb);
    const float metalness = Remap(saturate(GNMAO.SampleLevel(PointClampSampler, screenUV, 0).y), Raytracing.Metalness.x, Raytracing.Metalness.y);

    const float3 diffuseAlbedo = albedo * (1.0f - metalness);
    const float3 viewDirection = normalize(Camera.Position.xyz - positionWS);
    const float3 normalForSpecular = dot(viewDirection, faceNormal) >= 0.0f ? normal : -normal;
    const float3 faceNormalForSpecular = dot(viewDirection, faceNormal) >= 0.0f ? faceNormal : -faceNormal;
    const float NdotV = dot(normalForSpecular, viewDirection);
    const float normalBlend = saturate(NdotV / 0.1f);
    const float3 specularNormal = normalize(lerp(faceNormalForSpecular, normalForSpecular, normalBlend));
    uint specularSeed = idx.x * 1973u + idx.y * 9277u + GI12Params.FrameIndex * 26699u + 1u;
    const float2 specularSample = float2(GI12_Rand01(specularSeed), GI12_Rand01(specularSeed));
    const float3 sampledSpecularDirection = GI12_SampleGGXReflection(roughness, specularNormal, viewDirection, specularSample);
    const float3 specularDirection = dot(faceNormalForSpecular, sampledSpecularDirection) > 1e-4f
        ? sampledSpecularDirection
        : normalize(lerp(reflect(-viewDirection, faceNormalForSpecular), faceNormalForSpecular, 0.25f));

    // --- Diffuse: L1 screen-probe SH irradiance ---
    float3 diffuseIrradiance = float3(0.0f, 0.0f, 0.0f);
    float diffuseConf = 0.0f;
    float shSamples = 0.0f;
    if (GI12Params.ScreenProbes != 0) {
        const uint probeSize = max(ScreenProbes.ProbeSize, 4u);
        const uint countX = max(ScreenProbes.ProbeCount.x, 1u);
        const float2 probeCoord = (float2(idx) + 0.5f) / float(probeSize) - 0.5f;
        const int2 baseProbe = (int2)floor(probeCoord);
        const float2 probeFrac = frac(probeCoord);
        float4 sh[9];
        [unroll]
        for (uint l = 0; l < 9; ++l)
            sh[l] = 0.0f;
        float totalWeight = 0.0f;
        const float3 currentNormal = normalize(normal);
        // Capsaicin InterpolateScreenProbes weights: depth similarity
        // (linear-depth ratio) * normal pow8 (steep). Plane-distance guard
        // prevents probes ahead of the pixel plane leaking into background.
        const float linearDepth = length(Camera.Position.xyz - positionWS);
        [unroll]
        for (uint oy = 0; oy < 2; ++oy) {
            [unroll]
            for (uint ox = 0; ox < 2; ++ox) {
                int2 probeId = clamp(baseProbe + int2(ox, oy), int2(0, 0), int2(ScreenProbes.ProbeCount) - 1);
                float weight = (ox == 0 ? 1.0f - probeFrac.x : probeFrac.x) *
                    (oy == 0 ? 1.0f - probeFrac.y : probeFrac.y);
                const uint probeIndex = uint(probeId.y) * countX + uint(probeId.x);
                const float4 probePosition = ProbePosition[probeIndex];
                const float4 probeNormal = ProbeNormal[probeIndex];
                if (probePosition.w == 0.0f || probeNormal.w == 0.0f)
                    continue;
                const float3 probeN = normalize(probeNormal.xyz);
                const float planeDistance = abs(dot(positionWS - probePosition.xyz, probeN));
                // Projected probe footprint (Capsaicin cell_size = tan(fov)*probeSize/h).
                const float probeCellSize = max(HashGrid.MinCellSize * 0.25f,
                    linearDepth * abs(Camera.ProjInverse[1][1]) * float(probeSize) / max(float(Camera.RenderSize.y), 1.0f));
                if (planeDistance > probeCellSize)
                    continue;
                const float probeDepthVS = length(Camera.Position.xyz - probePosition.xyz);
                float depthWeight = saturate(1.0f - abs(probeDepthVS - linearDepth) / max(linearDepth, 1e-4f));
                const float normalWeight = pow(saturate(dot(currentNormal, probeN)), 8.0f);
                weight *= depthWeight * normalWeight;
                if (weight <= 1e-5f)
                    continue;
                const uint shBase = probeIndex * 9u;
                [unroll]
                for (uint l = 0; l < 9; ++l) {
                    uint4 packedSample = ProbeSH[shBase + l];
                    float4 sample = float4(float3(asint(packedSample.xyz)) * 0.001f, float(packedSample.w));
                    sh[l] += sample * weight;
                    if (l == 0)
                        totalWeight += sample.w * weight;
                }
            }
        }

        // Sparse screen-probe updates can leave all four bilinear slots
        // empty. Search outward by probe ring, like the source probe-mask
        // hierarchy, and use the nearest ring that contains valid probes.
        if (totalWeight <= 1e-5f) {
            const int2 centerProbe = (int2)floor(probeCoord + 0.5f);
            [loop]
            for (int radius = 1; radius <= 3 && totalWeight <= 1e-5f; ++radius) {
                [loop]
                for (int oy = -radius; oy <= radius; ++oy) {
                    [loop]
                    for (int ox = -radius; ox <= radius; ++ox) {
                        if (max(abs(ox), abs(oy)) != radius)
                            continue;
                        const int2 probeId = centerProbe + int2(ox, oy);
                        if (any(probeId < 0) || any(probeId >= int2(ScreenProbes.ProbeCount)))
                            continue;

                        const uint probeIndex = uint(probeId.y) * countX + uint(probeId.x);
                        const float4 probePosition = ProbePosition[probeIndex];
                        const float4 probeNormal = ProbeNormal[probeIndex];
                        if (probePosition.w == 0.0f || probeNormal.w == 0.0f)
                            continue;

                        const float3 probeNormalWS = normalize(probeNormal.xyz);
                        const float normalWeight = pow(saturate(dot(currentNormal, probeNormalWS)), 8.0f);
                        const float planeDistance = abs(dot(positionWS - probePosition.xyz, probeNormalWS));
                        const float ringCellSize = max(HashGrid.MinCellSize * 0.25f,
                            linearDepth * abs(Camera.ProjInverse[1][1]) * float(probeSize) / max(float(Camera.RenderSize.y), 1.0f));
                        if (planeDistance > ringCellSize)
                            continue;
                        const float probeDepthVS = length(Camera.Position.xyz - probePosition.xyz);
                        const float depthWeight = saturate(1.0f - abs(probeDepthVS - linearDepth) / max(linearDepth, 1e-4f));
                        const float2 probeDelta = float2(probeId) - probeCoord;
                        const float spatialWeight = rcp(1.0f + dot(probeDelta, probeDelta));
                        const float weight = spatialWeight * normalWeight * depthWeight;
                        if (weight <= 1e-5f)
                            continue;

                        const uint shBase = probeIndex * 9u;
                        [unroll]
                        for (uint l = 0; l < 9; ++l) {
                            const uint4 packedSample = ProbeSH[shBase + l];
                            const float4 sample = float4(float3(asint(packedSample.xyz)) * 0.001f,
                                float(packedSample.w));
                            sh[l] += sample * weight;
                            if (l == 0)
                                totalWeight += sample.w * weight;
                        }
                    }
                }
            }
        }

        shSamples = totalWeight;
        if (shSamples > 0.0f) {
            // GNMAO.z is occlusion amount (0 = unoccluded), same polarity as
            // VAOMAO.z in the GI pass: ao = 1-z is openness for the bent cone.
            // (Verified via debug view 1: passing z directly lit only the
            // occluded crevices and left open areas exactly black.)
            const float ao = saturate(1.0f - GNMAO.SampleLevel(PointClampSampler, screenUV, 0).z);
            diffuseIrradiance = GI12_SHEvaluateBentCone(normal, ao, sh) * float(probeSize);
            diffuseConf = saturate(shSamples / GI12_RESOLVE_CONVERGED_SAMPLES);
        }
    }

    // --- Specular/indirect: L2 hash-grid lookup ---
    float3 specularRadiance = float3(0.0f, 0.0f, 0.0f);
    float specularConf = 0.0f;
    float specSamples = 0.0f;
    if (GI12Params.HashGridCache != 0 && GI12Params.GlossyReflections != 0 && roughness < 0.95f &&
        dot(faceNormalForSpecular, specularDirection) > 1e-4f && dot(specularNormal, specularDirection) > 1e-4f) {
        RayDesc ray;
        ray.Origin = positionWS + faceNormalForSpecular * 0.5f;
        ray.Direction = specularDirection;
        ray.TMin = 0.1f;
        ray.TMax = 10000.0f;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES |
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(Scene, RAY_FLAG_NONE, INSTANCE_MASK, ray);
        query.Proceed();

        if (query.CommittedStatus() == COMMITTED_NOTHING) {
            const float3 f0 = lerp(0.04f.xxx, albedo, metalness);
            const float3 halfVector = normalize(viewDirection + specularDirection);
            const float fresnel = pow(1.0f - saturate(dot(viewDirection, halfVector)), 5.0f);
            const float3 brdf = evalMicrofacet(viewDirection, specularDirection, specularNormal,
                max(roughness * roughness, 1e-3f)) * (f0 + (1.0f - f0) * fresnel);
            const float proposalPdf = max(
                GI12_GGXReflectionPdf(roughness, specularNormal, viewDirection, specularDirection), 1e-4f);
            specularRadiance = GI12_ResolveSky(specularDirection) * brdf / proposalPdf;
            specSamples = GI12_RESOLVE_CONVERGED_SAMPLES;
            specularConf = 1.0f;
        } else {
            const float hitDist = query.CommittedRayT();
            const float3 reflectedHit = ray.Origin + specularDirection * hitDist;
            // Resolve the secondary-hit indirect cache; primary direct light
            // is never added to GI12 output.
            const float baseCellSize = GI12_CellSize(HashGrid, reflectedHit, Camera.Position.xyz, 0u);
            if (hitDist >= baseCellSize * 1.7320508f) {
                const uint cellMip0 = GI12_FindCellMip(HashGrid, Hash, reflectedHit, specularDirection,
                    Camera.Position.xyz, hitDist, 0u);
                if (cellMip0 < HashGrid.NumCells) {
                    float4 direct;
                    GI12_FilteredRadianceDirect(HashGrid, Value, cellMip0, direct);
                    float3 cached = direct.rgb / max(direct.w, 1.0f);
                    float samples = direct.w;
#ifdef GI12_MULTIBOUNCE
                    float4 indirect;
                    GI12_FilteredRadianceIndirect(HashGrid, ValueIndirect, cellMip0, indirect);
                    cached += indirect.rgb / max(indirect.w, 1.0f);
                    samples = max(samples, indirect.w);
#endif
                    if (samples >= 4.0f && dot(cached, cached) > 0.0f) {
                        const float voxelSize = GI12_CellSize(HashGrid, reflectedHit, Camera.Position.xyz, 0u);
                        const float r2 = min(roughness, 0.99f);
                        const float a2 = r2 * r2 * r2 * r2;
                        const float footprint = hitDist * sqrt(0.5f * a2 / max(1.0f - a2, 1e-4f));
                        if (footprint >= voxelSize || roughness <= 0.02f) {
                            const float3 f0 = lerp(0.04f.xxx, albedo, metalness);
                            const float3 halfVector = normalize(viewDirection + specularDirection);
                            const float fresnel = pow(1.0f - saturate(dot(viewDirection, halfVector)), 5.0f);
                            const float3 fresnelColor = f0 + (1.0f - f0) * fresnel;
                            const float3 brdf = evalMicrofacet(viewDirection, specularDirection, specularNormal,
                                max(roughness * roughness, 1e-3f)) * fresnelColor;
                            const float proposalPdf = max(
                                GI12_GGXReflectionPdf(roughness, specularNormal, viewDirection, specularDirection), 1e-4f);
                            specularRadiance = cached * brdf / proposalPdf;
                            specSamples = samples;
                            specularConf = saturate(specSamples / GI12_RESOLVE_CONVERGED_SAMPLES);
                        }
                    }
                }
            }
        }
    }

#ifdef GI12_DEBUG_VIEW
    // Debug visualization (see GI12Settings.DebugView). Writes to the same
    // outputs so no pipeline changes are needed; NRD will process the
    // visualization like a signal, which is fine for diagnosis.
    {
        float3 viz = float3(0.0f, 0.0f, 0.0f);
        #if GI12_DEBUG_VIEW == 1
            viz = diffuseIrradiance; // pre-demodulation diffuse
        #elif GI12_DEBUG_VIEW == 2
            viz = float3(diffuseConf, shSamples > 1e-5f ? 1.0f : 0.0f, specularConf);
        #elif GI12_DEBUG_VIEW == 3
        {
            const uint dbgProbeSize = max(ScreenProbes.ProbeSize, 4u);
            const uint dbgCountX = max(ScreenProbes.ProbeCount.x, 1u);
            const float2 dbgCoord = (float2(idx) + 0.5f) / float(dbgProbeSize) - 0.5f;
            const int2 dbgBase = clamp((int2)floor(dbgCoord), int2(0, 0), int2(ScreenProbes.ProbeCount) - 1);
            const uint dbgIdx = uint(dbgBase.y) * dbgCountX + uint(dbgBase.x);
            viz = (ProbePosition[dbgIdx].w > 0.0f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        }
        #elif GI12_DEBUG_VIEW == 4
            viz = specularRadiance; // pre-demodulation specular
        #elif GI12_DEBUG_VIEW == 5
        {
            // L2 secondary-hit direct contribution at this pixel's position:
            // verifies the fill path insert/merge content.
            const uint dbgCell = GI12_FindCellMip(HashGrid, Hash, positionWS, normal,
                Camera.Position.xyz, 10000.0f, 0u);
            if (dbgCell < HashGrid.NumCells) {
                float4 dbgDirect;
                GI12_FilteredRadianceDirect(HashGrid, Value, dbgCell, dbgDirect);
                viz = dbgDirect.w > 0.0f ? dbgDirect.rgb / max(dbgDirect.w, 1.0f)
                    : float3(0.0f, 0.0f, 0.0f);
            }
        }
        #endif
        #if defined(RAW_RADIANCE) && defined(NRD)
            #if GI12_DEBUG_VIEW == 4
                DiffuseOutput[idx] = GI12_PACK_DIFFUSE(float3(0.0f, 0.0f, 0.0f), 1.0f);
                SpecularOutput[idx] = GI12_PACK_SPECULAR(viz, 1.0f);
            #else
                DiffuseOutput[idx] = GI12_PACK_DIFFUSE(viz, 1.0f);
                SpecularOutput[idx] = GI12_PACK_SPECULAR(float3(0.0f, 0.0f, 0.0f), 1.0f);
            #endif
        #else
            Output[idx] = float4(viz, 1.0f);
        #endif
        return;
    }
#endif

#if defined(RAW_RADIANCE) && defined(NRD)
    const float3 F0 = PBR::F0(albedo, metalness);

    float3 diffFactor, specFactor;
    NRD_MaterialFactors(normal, viewDirection, diffuseAlbedo, F0, roughness, diffFactor, specFactor);

    // Demodulate exactly like the ray-traced GI pass so NRD/composite match.
    diffuseIrradiance /= max(diffFactor, 1e-4f);
    specularRadiance /= max(specFactor, 1e-4f);

    #if defined(NRD_REBLUR)
        const float diffNormHit = lerp(1.0f,
            REBLUR_FrontEnd_GetNormHitDist(depthVS, depthVS, Raytracing.HitDistSettings.xyz, roughness), diffuseConf);
        const float specNormHit = lerp(1.0f,
            REBLUR_FrontEnd_GetNormHitDist(depthVS, depthVS, Raytracing.HitDistSettings.xyz, roughness), specularConf);
        DiffuseOutput[idx] = GI12_PACK_DIFFUSE(diffuseIrradiance, diffNormHit);
        SpecularOutput[idx] = GI12_PACK_SPECULAR(specularRadiance, specNormHit);
    #else
        const float diffHit = lerp(depthVS * 8.0f, depthVS, diffuseConf);
        const float specHit = lerp(depthVS * 8.0f, depthVS, specularConf);
        DiffuseOutput[idx] = GI12_PACK_DIFFUSE(diffuseIrradiance, diffHit);
        SpecularOutput[idx] = GI12_PACK_SPECULAR(specularRadiance, specHit);
    #endif
#else
    // No denoiser: combine Lambert diffuse + specular like the GI composite.
    const float3 combined = diffuseIrradiance * diffuseAlbedo * (1.0f / 3.14159265f) + specularRadiance;
    Output[idx] = float4(combined, 1.0f);
#endif
}
