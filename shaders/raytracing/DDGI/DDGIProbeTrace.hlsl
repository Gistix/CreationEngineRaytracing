#include "raytracing/DDGI/DDGIRegisters.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Geometry.hlsli"
#include "raytracing/include/Materials/TexLODHelpers.hlsli"
#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"
#include "include/Lighting.hlsli"
#include "raytracing/include/Transparency.hlsli"
#include "raytracing/include/Rays.hlsli"

#include "ddgi/include/ProbeCommon.hlsl"
#include "ddgi/Irradiance.hlsl"

#if USE_RAY_QUERY
WAVE_SIZE(32)
[numthreads(8, 8, 1)]
void Main(uint3 DTid : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void Main()
#endif
{
    DDGIVolumeDescGPU volume = UnpackDDGIVolumeDescGPU(DDGIParams.Volume);

#if USE_RAY_QUERY
    uint rayIndex = DTid.x;
    uint probeIndexInPlane = DTid.y;
    uint planeIndex = DTid.z;
#else
    uint3 DTid = DispatchRaysIndex();
    uint rayIndex = DTid.x;
    uint probeIndexInPlane = DTid.y;
    uint planeIndex = DTid.z;
#endif

    int probesPerPlane = DDGIGetProbesPerPlane(volume.probeCounts);
    if (rayIndex >= (uint)volume.probeNumRays || probeIndexInPlane >= (uint)probesPerPlane || planeIndex >= (uint)volume.probeCounts.z)
        return;

    int probeIndex = int(planeIndex * probesPerPlane + probeIndexInPlane);
    int3 probeCoords = DDGIGetProbeCoords(probeIndex, volume);

    // If classified as inactive, write zero radiance and far distance
    float state = DDGILoadProbeState(probeIndex, ProbeData, volume);
    if (state == RTXGI_DDGI_PROBE_STATE_INACTIVE)
    {
        DDGIStoreProbeRayFrontfaceHit(RayData, DTid, volume, float3(0.0f, 0.0f, 0.0f), 1e27f);
        return;
    }

    float3 probeWorldPosition = DDGIGetProbeWorldPosition(probeCoords, volume, ProbeData);
    float3 rayDirection = DDGIGetProbeRayDirection(rayIndex, volume);

    uint randomSeed = InitRandomSeed(uint2(rayIndex, probeIndex), uint2(volume.probeNumRays, probesPerPlane * volume.probeCounts.z), Camera.FrameIndex);

    RayDesc ray;
    ray.Origin = probeWorldPosition;
    ray.Direction = rayDirection;
    ray.TMin = 0.05f;
    ray.TMax = volume.probeMaxRayDistance;

    Payload payload = TraceRayStandard(Scene, ray, randomSeed);

    if (!payload.Hit())
    {
        float3 skyRadiance = SampleSky(SkyHemisphere, rayDirection) * Raytracing.Sky;
        DDGIStoreProbeRayMiss(RayData, DTid, volume, skyRadiance);
        return;
    }

    Instance instance;
    LightingMaterialData material;
    RayCone rayCone = RayCone::make(0.0f, 0.0f);
    float3 localPosition = ray.Origin + rayDirection * payload.hitDistance;
    Surface surface = SurfaceMaker::make(localPosition, payload, rayDirection, rayCone, instance, material, false);

    bool isBackface = dot(surface.FaceNormal, -rayDirection) < 0.0f;
    if (isBackface)
    {
        DDGIStoreProbeRayBackfaceHit(RayData, DTid, volume, payload.hitDistance);
        return;
    }

    BRDFContext brdfContext = BRDFContext::make(surface, -rayDirection);
    bool isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
    if (!isEnter)
    {
        surface.FlipNormal();
        brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
    }
    AdjustShadingNormal(surface, brdfContext, true, false);
    StandardBSDF bsdf = StandardBSDF::make(surface, surface.Normal, brdfContext.ViewDirection, isEnter);

    float3 directRadiance = EvaluateDirectRadiance(material.Type, material.Feature, surface, brdfContext, instance, bsdf, randomSeed, false);
    directRadiance += surface.Emissive;

#if DDGI_MULTI_BOUNCE
    DDGIVolumeResources ddgiResources;
    ddgiResources.probeIrradiance = ProbeIrradiance;
    ddgiResources.probeDistance = ProbeDistance;
    ddgiResources.probeData = ProbeData;
    ddgiResources.bilinearSampler = BilinearClampSampler;

    float3 surfaceBias = DDGIGetSurfaceBias(surface.Normal, -rayDirection, volume);
    float3 ddgiIrradiance = DDGIGetVolumeIrradiance(surface.Position, surfaceBias, surface.Normal, volume, ddgiResources);
    directRadiance += (ddgiIrradiance / K_PI) * surface.DiffuseAlbedo;
#endif

    DDGIStoreProbeRayFrontfaceHit(RayData, DTid, volume, directRadiance, payload.hitDistance);
}
