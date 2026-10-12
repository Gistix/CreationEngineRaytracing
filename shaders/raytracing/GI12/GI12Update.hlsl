// GI-1.2 probe fill: RayQuery bounces per screen probe (Capsaicin GI1).
// Reconstructs probe world position/normal/roughness from the GBuffer and:
//  - diffuse: hemi-octahedral guided ray; skylight miss -> L2 cell + L1 SH,
//  - direct: uniform analytic-light pick with shadowed contribution -> L2,
//  - glossy (GI-1.1): roughness-jittered reflection ray; sky miss -> L2.
// Mirrors Capsaicin GI1 screen-probe spawn at quarter rate.

#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/GI12Data.hlsli"
#include "interop/Light.hlsli"

ConstantBuffer<CameraData> Camera : register(b0);
ConstantBuffer<GI12Data> GI12Params : register(b1);
ConstantBuffer<GI12ScreenProbesData> ScreenProbes : register(b2);
ConstantBuffer<GI12HashGridData> HashGrid : register(b3);
ConstantBuffer<RaytracingData> Raytracing : register(b4);
ConstantBuffer<FeatureData> Features : register(b5);

#include "include/Common.hlsli"
#include "include/ColorConversions.hlsli"
#include "include/PBR.hlsli"
#include "raytracing/include/GI12Cache.hlsli"
#include "include/Vulkan.hlsli"
#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"
#include "interop/Mesh.hlsli"
#include "interop/Instance.hlsli"
#include "interop/Transform.hlsli"
#include "interop/Properties.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Materials/TexLODHelpers.hlsli"

RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float4> SkyHemisphere : register(t2);
Texture2D<float4> NormalRoughness : register(t3);
StructuredBuffer<Light> Lights : register(t4);
StructuredBuffer<uint> PreviousHash : register(t5);
StructuredBuffer<uint2> PreviousValue : register(t6);
StructuredBuffer<uint2> PreviousValueIndirect : register(t7);
StructuredBuffer<float4> PreviousProbePosition : register(t8);
StructuredBuffer<float4> PreviousProbeNormal : register(t9);
StructuredBuffer<uint4> PreviousProbeSH : register(t10);
StructuredBuffer<float4> PreviousProbeRadiance : register(t11);
Texture2D<float3> Albedo : register(t12);
Texture2D<float3> GNMAO : register(t13);
Texture2D<float4> MotionVectors : register(t23);
StructuredBuffer<Instance> Instances : register(t14);
StructuredBuffer<Mesh> Meshes : register(t15);
StructuredBuffer<Transform> Transforms : register(t16);
ByteAddressBuffer MeshSlotRemap : register(t17);
ByteAddressBuffer PropertiesBuffer : register(t18);
Texture2D<float4> WaterDisplacementMap : register(t19);
Texture2D<float4> SkinDetailNormal : register(t20);
Texture2D<float4> ProjNoiseMap : register(t21);
Texture2D<float4> WaterFlowMap : register(t22);
ByteAddressBuffer Indices[] : register(t0, space1);
ByteAddressBuffer Vertices[] : register(t0, space2);
ByteAddressBuffer Materials[] : register(t0, space3);
Texture2D<float4> Textures[] : register(t0, space4);
VK_BINDING(5, 0) TextureCube<float4> CubeTextures[] : register(t0, space7);
VK_BINDING(6, 0) StructuredBuffer<float4> DynamicPositions[] : register(t0, space8);
SamplerState LinearClamp : register(s0);
SamplerState DefaultSampler : register(s1);
SamplerState ClampSampler : register(s2);
SamplerState PointWrapSampler : register(s3);
SamplerState PointClampSampler : register(s4);

#include "raytracing/include/Geometry.hlsli"
#include "include/SurfaceMaker.hlsli"

RWStructuredBuffer<float4> ProbePosition : register(u0);
RWStructuredBuffer<uint4> ProbeSH : register(u1);
RWStructuredBuffer<float4> ProbeNormal : register(u2);
RWStructuredBuffer<uint> Hash : register(u3);
RWStructuredBuffer<uint> TileLastTouched : register(u5);
RWStructuredBuffer<uint> TileUpdate : register(u6);
RWStructuredBuffer<uint> TileUpdateCount : register(u7);
RWStructuredBuffer<float4> ProbeRadiance : register(u8);
RWStructuredBuffer<uint> ProbeLastUpdated : register(u9);
RWStructuredBuffer<uint2> Value : register(u10);
RWStructuredBuffer<uint2> ValueIndirect : register(u11);
RWStructuredBuffer<uint> UpdateCellValue : register(u12);
RWStructuredBuffer<uint> UpdateCellValueIndirect : register(u13);

static const float GI12_QUANT = 1e4f;
static const float GI12_REUSE_RADIANCE_QUANT = 100.0f;
static const float GI12_REUSE_MAX_RADIANCE = 10000.0f;
static const float GI12_RAY_RADIANCE_QUANT = 100.0f;
static const float GI12_RAY_MAX_RADIANCE = 10000.0f;
groupshared float3 GI12_ProbePositionWS;
groupshared float3 GI12_ProbeNormalWS;
groupshared float GI12_ProbeRoughness;
groupshared uint GI12_ProbeValid;
groupshared uint2 GI12_ProbePixel;
groupshared uint2 GI12_SelectedProbeId;
groupshared uint GI12_TileHasSurface;
groupshared uint GI12_PreviousProbeIndex;
groupshared uint GI12_ProbeReprojected;
groupshared uint GI12_ReprojectionValues[4 * 256];
groupshared uint GI12_ReprojectionSampleCounts[256];
groupshared uint GI12_RayRadianceValues[4 * 256];
groupshared uint GI12_RaySampleCounts[256];
groupshared float GI12_GuidingCDF[256];
groupshared float GI12_GuidingTotal;

float GI12_Halton(uint index, uint radix)
{
    float value = 0.0f;
    float fraction = 1.0f / float(radix);
    while (index != 0u) {
        value += fraction * float(index % radix);
        index /= radix;
        fraction /= float(radix);
    }
    return value;
}

float3 GI12_SampleSky(float3 dir)
{
    // Matches SampleSky (Lighting.hlsli) + the GI pass: same polar mapping,
    // gamma-to-linear conversion, and the Raytracing.Sky intensity scale.
    // Omitting the scale/ freshly converted space leaves sky misses ~5x dim.
    float3 d = dir;
    d.z = max(d.z, 0.0f);
    float r = sqrt(max(1.0f - d.z, 0.0f));
    float phi = atan2(d.y, d.x);
    float2 uv = float2(cos(phi), sin(phi)) * r * 0.5f + 0.5f;
    return LLGammaToTrueLinear(SkyHemisphere.SampleLevel(LinearClamp, uv, 0).rgb) * Raytracing.Sky;
}

bool GI12_TraceClosest(float3 origin, float3 dir, float tMax, float tMin, out float hitT, out Payload hitPayload)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = tMin;
    ray.TMax = tMax;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, INSTANCE_MASK, ray);
    q.Proceed();
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        hitT = q.CommittedRayT();
        hitPayload.hitDistance = hitT;
        hitPayload.primitiveIndex = q.CommittedPrimitiveIndex();
        hitPayload.PackBarycentrics(q.CommittedTriangleBarycentrics());
        hitPayload.PackInstanceGeometryIndex(q.CommittedInstanceID(), q.CommittedGeometryIndex());
        hitPayload.randomSeed = 0u;
        return true;
    }
    hitT = -1.0f;
    hitPayload.hitDistance = -1.0f;
    hitPayload.primitiveIndex = 0u;
    hitPayload.PackBarycentrics(0.0f.xx);
    hitPayload.PackInstanceGeometryIndex(0u, 0u);
    hitPayload.randomSeed = 0u;
    return false;
}

bool GI12_Occluded(float3 origin, float3 dir, float tMax, float tMin, out float hitT)
{
    Payload ignoredPayload;
    return GI12_TraceClosest(origin, dir, tMax, tMin, hitT, ignoredPayload);
}

void GI12_AccumulateDirect(uint cell, float3 radiance)
{
    if (cell >= HashGrid.NumCells)
        return;
    // Same firefly ceiling as GI12_ProjectSH: keeps premultiplied
    // average*count inside f16 range (2048*16 < 65504) in Value buffers.
    const uint4 quantized = GI12_QuantizeCellRadiance(min(max(radiance, 0.0f), 2048.0f));
    if (dot(radiance, radiance) > 0.0f) {
        InterlockedAdd(UpdateCellValue[4u * cell + 0u], quantized.x);
        InterlockedAdd(UpdateCellValue[4u * cell + 1u], quantized.y);
        InterlockedAdd(UpdateCellValue[4u * cell + 2u], quantized.z);
    }
    InterlockedAdd(UpdateCellValue[4u * cell + 3u], quantized.w);
}

void GI12_AccumulateIndirect(uint cell, float3 radiance)
{
    if (cell >= HashGrid.NumCells)
        return;
    const uint4 quantized = GI12_QuantizeCellRadiance(min(max(radiance, 0.0f), 2048.0f));
    if (dot(radiance, radiance) > 0.0f) {
        InterlockedAdd(UpdateCellValueIndirect[4u * cell + 0u], quantized.x);
        InterlockedAdd(UpdateCellValueIndirect[4u * cell + 1u], quantized.y);
        InterlockedAdd(UpdateCellValueIndirect[4u * cell + 2u], quantized.z);
    }
    InterlockedAdd(UpdateCellValueIndirect[4u * cell + 3u], quantized.w);
}

void GI12_AccumulateAll(float3 positionWS, float3 direction, float3 eyeWS, float hitDistance,
    float3 radiance, bool indirect)
{
    // Capsaicin Populate*HandleHit: InsertCell once (mip0), bump DecayTile,
    // track UpdateTile on first touch of the frame. Only mip0 scratch
    // accumulates; UpdateTiles builds mips 1-3 by box filtering.
    uint tileIndex;
    bool isNewTile;
    const uint cellMip0 = GI12_InsertCell(HashGrid, Hash, positionWS, direction,
        eyeWS, hitDistance, tileIndex, isNewTile);
    if (cellMip0 == GI12_INVALID_ID || tileIndex == GI12_INVALID_ID)
        return;
    uint previousTouch;
    InterlockedExchange(TileLastTouched[tileIndex], HashGrid.FrameIndex, previousTouch);
    if (isNewTile || previousTouch != HashGrid.FrameIndex) {
        uint updateSlot;
        InterlockedAdd(TileUpdateCount[0], 1u, updateSlot);
        TileUpdate[updateSlot] = tileIndex;
        if (isNewTile) {
            const uint cellBase = tileIndex * HashGrid.NumCellsPerTile;
            [loop]
            for (uint o = 0u; o < HashGrid.NumCellsPerTileMip0; ++o) {
                const uint c = cellBase + HashGrid.FirstCellOffsetTileMip0 + o;
                Value[c] = uint2(0u, 0u);
                ValueIndirect[c] = uint2(0u, 0u);
            }
        }
    }
    if (indirect)
        GI12_AccumulateIndirect(cellMip0, radiance);
    else
        GI12_AccumulateDirect(cellMip0, radiance);
}

float GI12_PointFalloff(Light light, float dist)
{
    float t = saturate(dist * light.InvRadius);
    return 1.0f - t * t;
}

float GI12_SkyCloudOcclusion(float3 dir)
{
    // Mirrors EvalSkyOcclusion (Lighting.hlsli) for the sun path below.
    float3 d = dir;
    d.z = max(d.z, 0.0f);
    float r = sqrt(max(1.0f - d.z, 0.0f));
    float phi = atan2(d.y, d.x);
    float2 uv = float2(cos(phi), sin(phi)) * r * 0.5f + 0.5f;
    return lerp(1.0f, 1.0f - SkyHemisphere.SampleLevel(LinearClamp, uv, 0).a,
        Features.CloudShadows.Opacity);
}

float3 GI12_EvaluateSunDirect(Surface surface)
{
    const float3 toLight = normalize(Raytracing.DirectionalLight.Direction);
    const float ndl = saturate(dot(surface.Normal, toLight));
    if (ndl <= 0.0f)
        return 0.0f;
    float shadowHitT;
    // Shadow-ray origin matches the 0.5u bounce-ray offset: a 0.1u offset
    // sits inside the originating triangle's error band and self-shadows
    // nearly every secondary direct sample to zero.
    if (GI12_Occluded(surface.Position + surface.FaceNormal * 0.5f, toLight, 10000.0f, 0.1f, shadowHitT))
        return 0.0f;
    const float3 incoming = DirLightToLinear(Raytracing.DirectionalLight.Color.rgb) *
        Raytracing.Directional * GI12_SkyCloudOcclusion(toLight) * ndl;
    return surface.DiffuseAlbedo * (1.0f / GI12_PI) * incoming;
}

float3 GI12_EvaluateSurfaceDirect(Surface surface, inout uint seed)
{
    float3 direct = GI12_EvaluateSunDirect(surface);
    if (GI12Params.NumLights == 0u)
        return direct;

    const Light light = Lights[GI12_Rand(seed) % GI12Params.NumLights];
    if ((light.Flags & LightFlags::Active) == 0u)
        return direct;

    float3 toLight;
    float distanceToLight;
    float attenuation = 1.0f;
    if (light.Type == LightType::Directional) {
        toLight = normalize(-light.Direction);
        distanceToLight = 10000.0f;
    } else {
        const float3 lightVector = light.Position - surface.Position;
        distanceToLight = length(lightVector);
        toLight = lightVector / max(distanceToLight, 1e-4f);
        if (distanceToLight > light.Radius)
            return direct;
        attenuation = GI12_PointFalloff(light, distanceToLight);
        if (light.Type == LightType::Spot) {
            const float cosTheta = dot(normalize(-toLight), light.Direction);
            attenuation *= smoothstep(light.CosOuterAngle, light.CosInnerAngle, cosTheta);
        }
    }

    const float ndl = saturate(dot(surface.Normal, toLight));
    float shadowHitT;
    if (ndl <= 0.0f || attenuation <= 0.0f ||
        GI12_Occluded(surface.Position + surface.FaceNormal * 0.5f, toLight, distanceToLight, 0.1f, shadowHitT))
        return direct;

    const float3 incoming = PointLightToLinear(float3(light.Color.rgb),
        (light.Flags & LightFlags::LinearLight) != 0u) * light.Fade * attenuation * ndl * float(GI12Params.NumLights);
    return direct + surface.DiffuseAlbedo * (1.0f / GI12_PI) * incoming;
}

// Project a directional sample into the per-probe L1 SH (accumulates; the
// buffer is reset at the top of the kernel each frame). sampledDir is the
// ray direction toward the incoming light, consistent with GI12_SHEvaluate.
void GI12_ProjectSH(uint shBase, float3 sampledDir, float3 contrib)
{
    // Firefly ceiling: single-sample direct estimates scale with NumLights
    // (up to 1024) and would otherwise wrap the int32 accumulators below
    // into per-probe colored blocks. 2048 preserves sun-scale signals.
    contrib = min(contrib, 2048.0f);
    float basis[9];
    GI12_SHBasis(sampledDir, basis);
    for (uint l = 0; l < 9; ++l)
    {
        int3 quantized = (int3)(contrib * basis[l] * 1000.0f);
        uint original;
        InterlockedAdd(ProbeSH[shBase + l].x, asuint(quantized.x), original);
        InterlockedAdd(ProbeSH[shBase + l].y, asuint(quantized.y), original);
        InterlockedAdd(ProbeSH[shBase + l].z, asuint(quantized.z), original);
        InterlockedAdd(ProbeSH[shBase + l].w, 1u, original);
    }
}

// Multibounce bootstrap reads only the indirect cache, matching Capsaicin GI1.
float3 GI12_ReadCellAverage(float3 positionWS, float3 direction, float3 eyeWS, float hitDistance,
    out uint sampleCount)
{
    sampleCount = 0;
    const uint cellMip0 = GI12_FindCellMip(HashGrid, PreviousHash, positionWS, direction,
        eyeWS, hitDistance, 0u);
    if (cellMip0 >= HashGrid.NumCells)
        return 0.0f;

#ifdef GI12_MULTIBOUNCE
    float4 indirect;
    GI12_FilteredRadianceIndirect(HashGrid, PreviousValueIndirect, cellMip0, indirect);
    sampleCount = (uint)indirect.w;
    return indirect.rgb / max(indirect.w, 1.0f);
#else
    return 0.0f;
#endif
}

float3 GI12_SampleIndirectBounce(Surface surface, inout uint seed)
{
    const float3 direction = GI12_CosineSample(float2(GI12_Rand01(seed), GI12_Rand01(seed)), surface.Normal);
    const float3 origin = surface.Position + surface.FaceNormal * 0.5f;
    float hitT;
    Payload hitPayload;
    if (!GI12_TraceClosest(origin, direction, 10000.0f, 0.1f, hitT, hitPayload))
        return surface.DiffuseAlbedo * GI12_SampleSky(direction);

    const float3 hitPosition = origin + direction * hitT;
    Instance hitInstance;
    LightingMaterialData hitMaterial;
    RayCone hitRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * hitT,
        Raytracing.PixelConeSpreadAngle);
    Surface nextSurface = SurfaceMaker::make(hitPosition, hitPayload, direction, hitRayCone,
        hitInstance, hitMaterial, false);
    float3 radiance = nextSurface.Emissive + GI12_EvaluateSurfaceDirect(nextSurface, seed);

#ifdef GI12_MULTIBOUNCE
    if (GI12Params.HashGridCache != 0) {
        uint cachedSamples;
        radiance += GI12_ReadCellAverage(hitPosition, direction, Camera.Position.xyz, hitT, cachedSamples);
    }
#endif

    return surface.DiffuseAlbedo * radiance;
}

[numthreads(64, 1, 1)]
void Main(uint groupIndex : SV_GroupID, uint lane : SV_GroupThreadID)
{
    if (GI12Params.ScreenProbes == 0 && GI12Params.HashGridCache == 0)
        return;

    const uint2 probeCount = ScreenProbes.ProbeCount;
    const uint probeCountTotal = probeCount.x * probeCount.y;
    if (probeCountTotal == 0u)
        return;

    const uint probeSize = clamp(ScreenProbes.ProbeSize, 4u, 16u);
    const uint spawnTileSize = GI12Params.ScreenProbes != 0
        ? max(ScreenProbes.ProbeSpawnTileSize, probeSize)
        : probeSize;
    const uint probesPerSpawnTile = max(spawnTileSize / probeSize, 1u);
    const uint2 spawnTileCount = (probeCount + probesPerSpawnTile - 1u) / probesPerSpawnTile;
    const uint spawnTileCountTotal = spawnTileCount.x * spawnTileCount.y;
    if (groupIndex >= spawnTileCountTotal)
        return;

    const uint2 spawnTileId = uint2(groupIndex % spawnTileCount.x, groupIndex / spawnTileCount.x);
    const uint2 jitter = min(uint2(float2(GI12_Halton(GI12Params.FrameIndex + 1u, 2u),
        GI12_Halton(GI12Params.FrameIndex + 1u, 3u)) * float(spawnTileSize)), spawnTileSize - 1u);
    const uint2 pixel = min(spawnTileId * spawnTileSize + jitter, Camera.ScreenSize - 1u);
    if (lane == 0u) {
        uint2 selectedPixel = pixel;
        float2 selectedUV = GI12_UnjitterUV((float2(selectedPixel) + 0.5f) / float2(Camera.ScreenSize),
            Camera.Jitter, Camera.ScreenSize);
        float selectedDepth = Depth.SampleLevel(LinearClamp, selectedUV, 0);
        float selectedDepthVS = ScreenToViewDepth(selectedDepth, Camera.CameraData);
        float4 selectedNormal = NormalRoughness.SampleLevel(LinearClamp, selectedUV, 0);
        bool validSurface = selectedDepth < SKY_Z && selectedDepthVS >= FP_VIEW_Z &&
            dot(selectedNormal.xyz, selectedNormal.xyz) > 0.5f;

        if (!validSurface) {
            // If the Halton seed is sky, find the nearest surface anywhere in
            // this spawn tile and assign the sample to that probe cell.
            float bestDistanceSq = 3.402823466e+38f;
            const uint2 tileBegin = spawnTileId * spawnTileSize;
            const uint2 tileEnd = min(tileBegin + spawnTileSize, Camera.ScreenSize);
            [loop]
            for (uint y = tileBegin.y; y < tileEnd.y; ++y) {
                [loop]
                for (uint x = tileBegin.x; x < tileEnd.x; ++x) {
                    const uint2 candidatePixel = uint2(x, y);
                    const float2 candidateUV = GI12_UnjitterUV((float2(candidatePixel) + 0.5f) / float2(Camera.ScreenSize),
                        Camera.Jitter, Camera.ScreenSize);
                    const float candidateDepth = Depth.SampleLevel(LinearClamp, candidateUV, 0);
                    const float candidateDepthVS = ScreenToViewDepth(candidateDepth, Camera.CameraData);
                    if (candidateDepth >= SKY_Z || candidateDepthVS < FP_VIEW_Z)
                        continue;
                    const float3 candidateNormal = NormalRoughness.SampleLevel(LinearClamp, candidateUV, 0).xyz;
                    if (dot(candidateNormal, candidateNormal) <= 0.5f)
                        continue;
                    const float2 offset = float2(candidatePixel) - float2(pixel);
                    const float distanceSq = dot(offset, offset);
                    if (distanceSq < bestDistanceSq) {
                        bestDistanceSq = distanceSq;
                        selectedPixel = candidatePixel;
                    }
                }
            }
            validSurface = bestDistanceSq < 3.402823466e+38f;
        }
        GI12_ProbePixel = selectedPixel;
        GI12_SelectedProbeId = selectedPixel / probeSize;
        GI12_TileHasSurface = validSurface ? 1u : 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // One workgroup owns each spawn tile; the chosen pixel selects the probe
    // slot that receives the sample.
    if (GI12_TileHasSurface == 0u)
        return;
    const uint probeIndex = GI12_SelectedProbeId.y * probeCount.x + GI12_SelectedProbeId.x;
    const uint shBase = probeIndex * 9u;

    if (lane == 0u) {
        GI12_ProbeValid = 0u;
        GI12_PreviousProbeIndex = probeIndex;
        GI12_ProbeReprojected = 0u;

        uint2 probePixel = GI12_ProbePixel;
        float2 screenUVJittered = (float2(probePixel) + 0.5f) / float2(Camera.ScreenSize);
        float2 screenUV = GI12_UnjitterUV(screenUVJittered, Camera.Jitter, Camera.ScreenSize);
        float depth = Depth.SampleLevel(LinearClamp, screenUV, 0);
        float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
        float4 nr = NormalRoughness.SampleLevel(LinearClamp, screenUV, 0);
        float normalLengthSq = dot(nr.xyz, nr.xyz);
        if (depth < SKY_Z && depthVS >= FP_VIEW_Z && normalLengthSq > 0.5f) {
            const float3 positionVS = ScreenToViewPosition(screenUV, depthVS, Camera.NDCToView);
            const float3 positionCS = ViewToWorldPosition(positionVS, Camera.ViewInverse);
            const float3 positionWS = positionCS + Camera.Position.xyz;
            const float3 normalWS = nr.xyz * rsqrt(normalLengthSq);
            bool reprojected = false;
            uint previousProbeIndex = probeIndex;
            if (GI12Params.FrameIndex > 0u) {
                const float4 projectedPrevious = mul(Camera.PrevViewProj,
                    float4(positionWS - Camera.PositionPrev, 1.0f));
                if (projectedPrevious.w > 0.0f) {
                    const float2 cameraPreviousUV = projectedPrevious.xy / projectedPrevious.w *
                        float2(0.5f, -0.5f) + 0.5f;
                    const float2 motionUV = GI12Params.Pad0.y != 0u
                        ? MotionVectors.SampleLevel(PointClampSampler, screenUVJittered, 0).xy
                        : float2(0.0f, 0.0f);
                    const float2 previousUV = GI12Params.Pad0.y != 0u
                        ? screenUVJittered + motionUV
                        : cameraPreviousUV;
                    if (all(previousUV >= 0.0f) && all(previousUV < 1.0f)) {
                        const uint2 previousPixel = min(uint2(previousUV * float2(Camera.ScreenSize)), Camera.ScreenSize - 1u);
                        const uint2 previousProbe = previousPixel / probeSize;
                        previousProbeIndex = previousProbe.y * ScreenProbes.ProbeCount.x + previousProbe.x;
                        const float4 oldPosition = PreviousProbePosition[previousProbeIndex];
                        const float4 oldNormal = PreviousProbeNormal[previousProbeIndex];
                        const float4 oldClip = mul(Camera.PrevViewProj,
                            float4(oldPosition.xyz - Camera.PositionPrev, 1.0f));
                        const float2 oldUV = oldClip.w > 0.0f
                            ? oldClip.xy / oldClip.w * float2(0.5f, -0.5f) + 0.5f
                            : -1.0f.xx;
                        const float2 probeUVRadius = float2(probeSize, probeSize) /
                            float2(Camera.ScreenSize) * 1.5f;
                        reprojected = oldPosition.w > 0.0f && oldNormal.w > 0.0f && oldClip.w > 0.0f &&
                            all(abs(oldUV - previousUV) <= probeUVRadius) &&
                            dot(normalWS, normalize(oldNormal.xyz)) >= 0.8f;
                    }
                }
            }
                [unroll]
                for (uint l = 0; l < 9; ++l)
                    ProbeSH[shBase + l] = reprojected ? PreviousProbeSH[previousProbeIndex * 9u + l] : 0u;
                GI12_ProbePositionWS = positionWS;
                GI12_ProbeNormalWS = normalWS;
                GI12_ProbeRoughness = PBR::Roughness(saturate(nr.w), Raytracing.Roughness.x, Raytracing.Roughness.y);
                ProbePosition[probeIndex] = float4(positionWS, 1.0f);
                ProbeNormal[probeIndex] = float4(normalWS, 1.0f);
                ProbeLastUpdated[probeIndex] = GI12Params.FrameIndex;
                GI12_PreviousProbeIndex = previousProbeIndex;
                GI12_ProbeReprojected = reprojected ? 1u : 0u;
            GI12_ProbeValid = 1u;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (GI12_ProbeValid == 0u)
        return;

    const uint probeCellCount = probeSize * probeSize;
    for (uint cellIndex = lane; cellIndex < probeCellCount; cellIndex += 64u) {
        GI12_ReprojectionValues[cellIndex * 4u + 0u] = 0u;
        GI12_ReprojectionValues[cellIndex * 4u + 1u] = 0u;
        GI12_ReprojectionValues[cellIndex * 4u + 2u] = 0u;
        GI12_ReprojectionValues[cellIndex * 4u + 3u] = 0u;
        GI12_ReprojectionSampleCounts[cellIndex] = 0u;
        GI12_RayRadianceValues[cellIndex * 4u + 0u] = 0u;
        GI12_RayRadianceValues[cellIndex * 4u + 1u] = 0u;
        GI12_RayRadianceValues[cellIndex * 4u + 2u] = 0u;
        GI12_RayRadianceValues[cellIndex * 4u + 3u] = 0u;
        GI12_RaySampleCounts[cellIndex] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // Reproject prior-frame neighboring probes into this probe's direction
    // bins. Plane and normal checks prevent reuse across surface boundaries.
    if (GI12Params.FrameIndex > 0u) {
        const float probeCellSize = max(HashGrid.MinCellSize * 2.0f,
            length(Camera.Position.xyz - GI12_ProbePositionWS) * abs(Camera.ProjInverse[1][1]) *
                float(probeSize) / max(float(Camera.ScreenSize.y), 1.0f));
        const int2 probeGridSize = int2(ScreenProbes.ProbeCount);
        const int2 historyProbeId = int2(GI12_PreviousProbeIndex % ScreenProbes.ProbeCount.x,
            GI12_PreviousProbeIndex / ScreenProbes.ProbeCount.x);
        [loop]
        for (uint sourceIndex = lane; sourceIndex < probeCellCount; sourceIndex += 64u) {
            const uint2 sourceCell = uint2(sourceIndex % probeSize, sourceIndex / probeSize);
            [unroll]
            for (int offsetY = -1; offsetY <= 1; ++offsetY) {
                [unroll]
                for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                    const int2 neighborId = historyProbeId + int2(offsetX, offsetY);
                    if (any(neighborId < 0) || any(neighborId >= probeGridSize))
                        continue;
                    const uint neighborIndex = uint(neighborId.y) * ScreenProbes.ProbeCount.x + uint(neighborId.x);
                    const float4 neighborPosition = PreviousProbePosition[neighborIndex];
                    const float4 neighborNormalData = PreviousProbeNormal[neighborIndex];
                    if (neighborPosition.w <= 0.0f || neighborNormalData.w <= 0.0f)
                        continue;
                    const float3 neighborNormal = normalize(neighborNormalData.xyz);
                    if (dot(neighborNormal, GI12_ProbeNormalWS) < 0.8f ||
                        abs(dot(neighborPosition.xyz - GI12_ProbePositionWS, GI12_ProbeNormalWS)) > probeCellSize)
                        continue;

                    const float4 cachedRadiance = PreviousProbeRadiance[neighborIndex * probeCellCount + sourceIndex];
                    if (cachedRadiance.w <= 0.0f)
                        continue;
                    const float2 sourceUV = (float2(sourceCell) + 0.5f) / float(probeSize);
                    const float3 sourceDirection = GI12_ProbeRayDirection(sourceUV, neighborNormal);
                    const float3 hitPosition = neighborPosition.xyz + neighborNormal * 0.5f + sourceDirection * cachedRadiance.w;
                    const float3 reprojectedVector = hitPosition - GI12_ProbePositionWS;
                    const float reprojectedDistance = length(reprojectedVector);
                    if (reprojectedDistance <= 1e-4f)
                        continue;
                    const float3 reprojectedDirection = reprojectedVector / reprojectedDistance;
                    if (dot(GI12_ProbeNormalWS, reprojectedDirection) <= 0.0f)
                        continue;

                    const uint targetCell = GI12_ProbeCellForDirection(reprojectedDirection, GI12_ProbeNormalWS, probeSize);
                    const uint targetBase = targetCell * 4u;
                    const uint3 quantizedRadiance = (uint3)(min(max(cachedRadiance.rgb, 0.0f), GI12_REUSE_MAX_RADIANCE) * GI12_REUSE_RADIANCE_QUANT);
                    uint ignored;
                    InterlockedAdd(GI12_ReprojectionValues[targetBase + 0u], quantizedRadiance.x, ignored);
                    InterlockedAdd(GI12_ReprojectionValues[targetBase + 1u], quantizedRadiance.y, ignored);
                    InterlockedAdd(GI12_ReprojectionValues[targetBase + 2u], quantizedRadiance.z, ignored);
                    InterlockedAdd(GI12_ReprojectionValues[targetBase + 3u], (uint)min(reprojectedDistance, 10000.0f), ignored);
                    InterlockedAdd(GI12_ReprojectionSampleCounts[targetCell], 1u, ignored);
                }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (lane == 0u) {
        float cumulativeWeight = 0.0f;
        [loop]
        for (uint cellIndex = 0u; cellIndex < probeCellCount; ++cellIndex) {
            const uint reuseCount = GI12_ReprojectionSampleCounts[cellIndex];
            float weight = 0.0f;
            if (reuseCount > 0u) {
                const uint reuseBase = cellIndex * 4u;
                const float3 radiance = float3(GI12_ReprojectionValues[reuseBase + 0u],
                    GI12_ReprojectionValues[reuseBase + 1u], GI12_ReprojectionValues[reuseBase + 2u]) /
                    (GI12_REUSE_RADIANCE_QUANT * float(reuseCount));
                        const float2 cellUV = (float2(cellIndex % probeSize, cellIndex / probeSize) + 0.5f) /
                            float(probeSize);
                        const float cosine = GI12_HemiOctahedron(cellUV).z;
                        weight = dot(radiance, float3(0.2126f, 0.7152f, 0.0722f)) * cosine;
            }
            cumulativeWeight += max(weight, 0.0f);
            GI12_GuidingCDF[cellIndex] = cumulativeWeight;
        }
        GI12_GuidingTotal = cumulativeWeight;
    }
    GroupMemoryBarrierWithGroupSync();

    // Trace guided hemisphere samples and store results by direction cell.
    const uint rayCount = probeSize * probeSize;
    for (uint sampleIndex = lane; sampleIndex < rayCount; sampleIndex += 64u) {
        uint2 sampleCell = uint2(sampleIndex % probeSize, sampleIndex / probeSize);
        uint seed = GI12_Hash(uint3(GI12_ProbePixel + sampleCell,
            GI12Params.FrameIndex + sampleIndex * 0x9e3779b9u + 1u));
        const float3 positionWS = GI12_ProbePositionWS;
        const float3 normalWS = GI12_ProbeNormalWS;
        const float roughness = GI12_ProbeRoughness;
        const float3 origin = positionWS + normalWS * 0.5f;

        // --- BRDF-guided bounce. Miss: skylight. Hit: multibounce
        // bootstrap from the previous-frame hash-grid snapshot. ---
        float2 sampleUV = (float2(sampleCell) + float2(GI12_Rand01(seed), GI12_Rand01(seed))) / float(probeSize);
        float3 dir = GI12_ProbeRayDirection(sampleUV, normalWS);
        const bool guidingEnabled = GI12_GuidingTotal > 1e-6f;
        if (GI12_GuidingTotal > 1e-6f) {
            const float sampleWeight = max(GI12_Rand01(seed) * GI12_GuidingTotal, 1e-8f);
            uint low = 0u;
            uint high = probeCellCount;
            [loop]
            while (low < high) {
                const uint mid = (low + high) >> 1u;
                if (GI12_GuidingCDF[mid] < sampleWeight)
                    low = mid + 1u;
                else
                    high = mid;
            }
            const uint guidedCell = min(low, probeCellCount - 1u);
            const uint2 guidedCell2D = uint2(guidedCell % probeSize, guidedCell / probeSize);
            sampleUV = (float2(guidedCell2D) + float2(GI12_Rand01(seed), GI12_Rand01(seed))) / float(probeSize);
            dir = GI12_ProbeRayDirection(sampleUV, normalWS);
        }

        uint targetCell = GI12_ProbeCellForDirection(dir, normalWS, probeSize);
        const float3 tangent = normalize(cross(abs(normalWS.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f), normalWS));
        const float3 bitangent = cross(normalWS, tangent);
        const float3 localDirection = float3(dot(dir, tangent), dot(dir, bitangent), dot(dir, normalWS));
        const float2 directionUV = GI12_HemiOctahedronInverse(localDirection);
        const float directionJacobian = max(GI12_HemiOctahedronInversePdf(directionUV), 1e-8f);
        float diffusePdf = rcp(directionJacobian);
        if (guidingEnabled) {
            const float previousCDF = targetCell == 0u ? 0.0f : GI12_GuidingCDF[targetCell - 1u];
            const float cellWeight = max(GI12_GuidingCDF[targetCell] - previousCDF, 0.0f);
            diffusePdf = cellWeight * float(probeCellCount) * directionJacobian /
                max(GI12_GuidingTotal, 1e-8f);
        }
        const float inversePdf = rcp(max(diffusePdf, 1e-8f));
        float hitT = -1.0f;
        float3 probeRadiance = 0.0f;
        float probeHitDistance = 10000.0f;
        Payload hitPayload;
        if (!GI12_TraceClosest(origin, dir, 10000.0f, 0.1f, hitT, hitPayload)) {
            // Use the environment radiance directly; SH projection below
            // applies the inverse mixture PDF.
            float3 skyRadiance = GI12_SampleSky(dir);
            probeRadiance = skyRadiance;
        } else {
            probeHitDistance = hitT;
            const float3 hitPositionWS = origin + dir * hitT;
            Instance hitInstance;
            LightingMaterialData hitMaterial;
            RayCone hitRayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * hitT,
                Raytracing.PixelConeSpreadAngle);
            Surface hitSurface = SurfaceMaker::make(hitPositionWS, hitPayload, dir, hitRayCone,
                hitInstance, hitMaterial, false);
            const float3 directRadiance = hitSurface.Emissive + GI12_EvaluateSurfaceDirect(hitSurface, seed);
            float3 indirectRadiance = 0.0f;
            if (GI12Params.HashGridCache != 0 && GI12Params.Pad0.x != 0u &&
                GI12_Rand01(seed) >= HashGrid.DiscardMultibounceProbability)
                indirectRadiance = GI12_SampleIndirectBounce(hitSurface, seed) /
                    max(1.0f - HashGrid.DiscardMultibounceProbability, 1e-4f);

            probeRadiance = directRadiance + indirectRadiance;
            if (GI12Params.HashGridCache != 0) {
                GI12_AccumulateAll(hitPositionWS, dir, Camera.Position.xyz, hitT,
                    directRadiance, false);
                if (GI12Params.Pad0.x != 0u)
                    GI12_AccumulateAll(hitPositionWS, dir, Camera.Position.xyz, hitT,
                        indirectRadiance, true);
            }
        }
        if (GI12Params.ScreenProbes != 0) {
            targetCell = GI12_ProbeCellForDirection(dir, normalWS, probeSize);
            float4 sample = float4(probeRadiance, probeHitDistance);
            if (GI12_ProbeReprojected != 0u) {
                const uint previousRadianceIndex = GI12_PreviousProbeIndex * probeSize * probeSize + targetCell;
                const float4 previous = PreviousProbeRadiance[previousRadianceIndex];
                if (previous.w > 0.0f) {
                    sample = lerp(sample, previous, 0.2f);
                }
            }
            const uint reusedCount = GI12_ReprojectionSampleCounts[targetCell];
            if (reusedCount > 0u) {
                const uint reuseBase = targetCell * 4u;
                const float4 reused = float4(
                    float3(GI12_ReprojectionValues[reuseBase + 0u], GI12_ReprojectionValues[reuseBase + 1u],
                        GI12_ReprojectionValues[reuseBase + 2u]) / (GI12_REUSE_RADIANCE_QUANT * float(reusedCount)),
                    float(GI12_ReprojectionValues[reuseBase + 3u]) / float(reusedCount));
                sample = lerp(sample, reused, 0.25f);
            }
            GI12_ProjectSH(shBase, dir, sample.rgb * inversePdf);
            const uint rayBase = targetCell * 4u;
            const uint3 quantizedSample = (uint3)(min(max(sample.rgb, 0.0f), GI12_RAY_MAX_RADIANCE) * GI12_RAY_RADIANCE_QUANT);
            uint ignored;
            InterlockedAdd(GI12_RayRadianceValues[rayBase + 0u], quantizedSample.x, ignored);
            InterlockedAdd(GI12_RayRadianceValues[rayBase + 1u], quantizedSample.y, ignored);
            InterlockedAdd(GI12_RayRadianceValues[rayBase + 2u], quantizedSample.z, ignored);
            InterlockedAdd(GI12_RayRadianceValues[rayBase + 3u], (uint)min(sample.w, 10000.0f), ignored);
            InterlockedAdd(GI12_RaySampleCounts[targetCell], 1u, ignored);
        }
    // NOTE: no analytic lights are evaluated at the probe itself here. The
    // probe is a primary vertex (from the raster GBuffer) and this pass is
    // indirect-only like the GI pass it replaces: analytic sun/point lights
    // enter exclusively at secondary vertices via GI12_EvaluateSurfaceDirect
    // (one-bounce indirect), plus sky misses and emissive hits. Evaluating
    // lights at the probe would inject direct lighting (with sharp sun
    // shadows) into the diffuse cache and double-count the game's own
    // direct lighting.

    }

    GroupMemoryBarrierWithGroupSync();
    if (GI12Params.ScreenProbes != 0) {
        for (uint cellIndex = lane; cellIndex < probeCellCount; cellIndex += 64u) {
            const uint rayCountForCell = GI12_RaySampleCounts[cellIndex];
            const uint radianceIndex = probeIndex * probeCellCount + cellIndex;
            if (rayCountForCell > 0u) {
                const uint rayBase = cellIndex * 4u;
                ProbeRadiance[radianceIndex] = float4(
                    float3(GI12_RayRadianceValues[rayBase + 0u], GI12_RayRadianceValues[rayBase + 1u],
                        GI12_RayRadianceValues[rayBase + 2u]) /
                        (GI12_RAY_RADIANCE_QUANT * float(rayCountForCell)),
                    float(GI12_RayRadianceValues[rayBase + 3u]) / float(rayCountForCell));
            } else {
                const uint reuseCount = GI12_ReprojectionSampleCounts[cellIndex];
                if (reuseCount > 0u) {
                    const uint reuseBase = cellIndex * 4u;
                    ProbeRadiance[radianceIndex] = float4(
                        float3(GI12_ReprojectionValues[reuseBase + 0u], GI12_ReprojectionValues[reuseBase + 1u],
                            GI12_ReprojectionValues[reuseBase + 2u]) /
                            (GI12_REUSE_RADIANCE_QUANT * float(reuseCount)),
                        float(GI12_ReprojectionValues[reuseBase + 3u]) / float(reuseCount));
                } else if (GI12_ProbeReprojected != 0u) {
                    const uint previousRadianceIndex = GI12_PreviousProbeIndex * probeCellCount + cellIndex;
                    ProbeRadiance[radianceIndex] = PreviousProbeRadiance[previousRadianceIndex];
                } else {
                    ProbeRadiance[radianceIndex] = 0.0f;
                }
            }
        }
    }
}
