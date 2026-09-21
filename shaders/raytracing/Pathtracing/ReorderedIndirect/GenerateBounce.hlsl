#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/SharedData.hlsli"
#include "interop/RayRecord.hlsli"

ConstantBuffer<CameraData>     Camera           : register(b0);
ConstantBuffer<RaytracingData> Raytracing       : register(b1);
ConstantBuffer<FeatureData>    Features         : register(b2);

#include "interop/Material/MaterialBaseData.hlsli"
#if defined(SKYRIM)
#   include "interop/Material/Skyrim/LightingMaterialData.hlsli"
#elif defined(FALLOUT4)
#   include "interop/Material/Fallout4/LightingMaterialData.hlsli"
#endif

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "include/Surface.hlsli"
#include "include/PBR.hlsli"
#include "raytracing/include/Materials/BSDF.hlsli"
#include "raytracing/include/RayOffset.hlsli"
#include "raytracing/include/SER.hlsli"
#include "raytracing/Pathtracing/ReorderedIndirect/WaveAggregation.hlsli"

Texture2D<float>               Depth            : register(t0);
Texture2D<float4>              Albedo           : register(t1);
Texture2D<float4>              EmissiveMetallic : register(t2);
Texture2D<float4>              NormalRoughness  : register(t3);
Texture2D<uint>                Material         : register(t4);

RWStructuredBuffer<RayRecord>  RayRecords       : register(u0);
RWStructuredBuffer<uint>       RayKeys          : register(u1);
RWStructuredBuffer<uint>       CounterBuffer    : register(u2);
RWStructuredBuffer<uint>       BinHistogram     : register(u3);

bool ComputeTangentSpace(inout Surface surface, const bool ignoreTangent)
{
    float NdotT = dot(surface.GeomTangent, surface.Normal);
    bool nonParallel = abs(NdotT) < 0.9999f;
    bool nonZero = dot(surface.GeomTangent, surface.GeomTangent) > 0.f;

    bool valid = nonZero && nonParallel;
    if (!ignoreTangent && valid)
    {
        surface.Tangent = normalize(surface.GeomTangent - surface.Normal * NdotT);
        surface.Bitangent = cross(surface.Normal, surface.Tangent);
    }
    else
    {
        surface.Tangent = perp_stark(surface.Normal);
        surface.Bitangent = cross(surface.Normal, surface.Tangent);
    }

    return valid;
}

void AdjustShadingNormal(inout Surface surface, BRDFContext brdfContext, uniform bool recomputeTangentSpace, const bool ignoreTangent)
{
    float3 Ng = dot(brdfContext.ViewDirection, surface.FaceNormal) >= 0.f ? surface.FaceNormal : -surface.FaceNormal;
    float signN = dot(surface.Normal, Ng) >= 0.f ? 1.f : -1.f;
    float3 Ns = signN * surface.Normal;

    const float kCosThetaThreshold = 0.1f;
    float cosTheta = dot(brdfContext.ViewDirection, Ns);
    if (cosTheta <= kCosThetaThreshold)
    {
        float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
        surface.Normal = signN * normalize(lerp(Ng, Ns, t));
    }
    if (cosTheta <= kCosThetaThreshold || recomputeTangentSpace)
        ComputeTangentSpace(surface, ignoreTangent);
}

Surface MakePrimarySurface(float3 position, float3 faceNormal, float3 normal, float3 tangent, float3 bitangent, float3 albedo, float roughness, float metallic, float3 emissive, float ao)
{
    Surface surface = (Surface)0;
    surface.Primary = false;
    surface.Position = position;
    surface.CameraRelativePosition = position - Camera.Position;
    surface.PrevCameraRelativePosition = surface.CameraRelativePosition + (Camera.Position - Camera.PositionPrev);
    surface.FaceNormal = faceNormal;
    surface.MipLevel = 0.0f + Raytracing.TexLODBias;
    surface.PositionError = max(abs(position.x), max(abs(position.y), abs(position.z)));
#if USE_SIA_INTERPOLATION
    surface.SIAOffset = 0.0f;
#endif
    surface.GeomNormal = faceNormal;
    surface.GeomTangent = tangent;
    surface.Normal = normal;
    surface.Tangent = tangent;
    surface.Bitangent = bitangent;
    surface.Albedo = albedo;
    surface.TransmissionColor = float3(0.0f, 0.0f, 0.0f);
    surface.VolumeAbsorption = float3(0.0f, 0.0f, 0.0f);
    surface.Emissive = emissive * Raytracing.Emissive;
    surface.Roughness = PBR::Roughness(roughness, Raytracing.Roughness.x, Raytracing.Roughness.y);
    surface.Metallic = Remap(metallic, Raytracing.Metalness.x, Raytracing.Metalness.y);
    surface.AO = ao;
    surface.DiffuseAlbedo = surface.Albedo * (1.0f - surface.Metallic);
    surface.F0 = PBR::F0(albedo, surface.Metallic);
    surface.IOR = F0toIOR(surface.F0);
    surface.CoatColor = float3(1.0f, 1.0f, 1.0f);
    surface.CoatStrength = 0.0f;
    surface.CoatRoughness = 0.0f;
    surface.CoatF0 = float3(0.04f, 0.04f, 0.04f);
    surface.CoatNormal = normal;
    surface.CoatTangent = tangent;
    surface.CoatBitangent = bitangent;
    surface.FuzzColor = float3(0.0f, 0.0f, 0.0f);
    surface.FuzzWeight = 0.0f;
    return surface;
}

WAVE_SIZE(32)
[numthreads(16, 16, 1)]
void Main(uint2 idx : SV_DispatchThreadID)
{
    uint2 size = Camera.RenderSize;
    if (any(idx >= size))
        return;

    const half4 albedo = (half4)Albedo[idx];
    if (albedo.a < 0.5f)
        return;

    const float depth = Depth[idx];

    uint randomSeed = InitRandomSeed(idx, size, Camera.FrameIndex);

    const half4 emissiveMetallic = (half4)EmissiveMetallic[idx];
    const half4 normalRoughness = (half4)NormalRoughness[idx];
    const uint material = Material[idx];

    const uint16_t materialFeature = uint16_t((material >> 8) & 0xFFu);

    const float depthVS = ScreenToViewDepth(depth, Camera.CameraData);
    const float2 uv = float2(idx + 0.5f) / size;

    const float3 positionVS = ScreenToViewPosition(uv, depthVS, Camera.NDCToView);
    const float3 positionCS = ViewToWorldPosition(positionVS, Camera.ViewInverse);
    const float3 positionWS = positionCS + Camera.Position.xyz;

    const float hitDistance = max(length(positionCS), 1e-4f);
    const float3 normalWS = normalRoughness.xyz;

    float3 tangentWS, bitangentWS;
    CreateOrthonormalBasis(normalWS, tangentWS, bitangentWS);

    const float3 faceNormal = normalWS;

    Surface sourceSurface = MakePrimarySurface(positionWS, faceNormal, normalWS, tangentWS, bitangentWS, albedo.xyz, normalRoughness.w, emissiveMetallic.w, emissiveMetallic.xyz, 1.0f);
    BRDFContext sourceBRDFContext = BRDFContext::make(sourceSurface, -positionCS / hitDistance);

    bool sourceIsEnter = dot(sourceSurface.FaceNormal, sourceBRDFContext.ViewDirection) >= 0.0f;
    if (!sourceIsEnter)
    {
        sourceSurface.FlipNormal();
        sourceBRDFContext.NdotV = saturate(dot(sourceSurface.Normal, sourceBRDFContext.ViewDirection));
    }

    AdjustShadingNormal(sourceSurface, sourceBRDFContext, true, false);

    StandardBSDF sourceBSDF = StandardBSDF::make(sourceSurface, sourceSurface.Normal, sourceBRDFContext.ViewDirection, sourceIsEnter);

    BSDFSample firstSample;
    if (!sourceBSDF.SampleBSDF(sourceBRDFContext, materialFeature, sourceSurface, firstSample, randomSeed))
        return;

    half3 throughput = (half3)firstSample.weight * (firstSample.isLobe(LobeType::Transmission) ? 1.0h : (half)sourceSurface.AO);
    if (all(throughput <= 0.0h))
        return;

    float3 firstFaceNormal = dot(sourceBRDFContext.ViewDirection, sourceSurface.FaceNormal) >= 0.0f ? sourceSurface.FaceNormal : -sourceSurface.FaceNormal;
    bool firstHasTransmission = firstSample.isLobe(LobeType::Transmission);

    float3 rayOrigin;
#if USE_SIA_INTERPOLATION
    rayOrigin = OffsetRaySIA(sourceSurface.Position, firstFaceNormal, sourceSurface.SIAOffset, firstHasTransmission);
#else
    rayOrigin = OffsetRay(sourceSurface.Position, firstFaceNormal, sourceSurface.PositionError, firstHasTransmission);
#endif
    float3 rayDirection = (float3)firstSample.wo;

    uint rayKey = SER_CalculateSpatialDirectionalKey12bit(idx, size, rayDirection);

    // Wave-level contiguous index allocation for CounterBuffer[0]
    uint waveActiveCount = WaveActiveCountBits(true);
    uint wavePrefix = WavePrefixCountBits(true);
    uint waveBaseIdx = 0;
    bool isFirstActiveLane = (WaveGetLaneIndex() == WaveReadLaneFirst(WaveGetLaneIndex()));
    if (isFirstActiveLane)
    {
        InterlockedAdd(CounterBuffer[0], waveActiveCount, waveBaseIdx);
    }
    waveBaseIdx = WaveReadLaneFirst(waveBaseIdx);
    uint rayIdx = waveBaseIdx + wavePrefix;

    RayRecord record;
    record.Origin = rayOrigin;
    record.PixelCoord = (idx.y << 16) | (idx.x & 0xFFFF);
    record.Direction = rayDirection;
    record.RandomSeed = randomSeed;
    record.Throughput = (float3)throughput;
    record.Pad = 0.0f;

    RayRecords[rayIdx] = record;
    RayKeys[rayIdx] = rayKey;

    // Wave-level key aggregation for BinHistogram
    WaveKeyMatch match = WaveMatchKey(rayKey);
    if (match.isLeader)
    {
        InterlockedAdd(BinHistogram[rayKey], match.matchCount);
    }
}
