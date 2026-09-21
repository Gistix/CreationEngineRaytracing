#include "raytracing/Pathtracing/ReorderedIndirect/Registers.hlsli"

#include "include/Common.hlsli"
#include "raytracing/include/Common.hlsli"
#include "include/WaveSize.hlsli"
#include "raytracing/include/Payload.hlsli"

#include "include/Surface.hlsli"
#include "include/SurfaceMaker.hlsli"

#include "include/Lighting.hlsli"
#include "raytracing/include/Materials/BSDF.hlsli"

#include "raytracing/include/Rays.hlsli"

#if USE_RAY_QUERY
WAVE_SIZE(32)
[numthreads(64, 1, 1)]
void Main(uint idx : SV_DispatchThreadID)
#else
[shader("raygeneration")]
void Main()
#endif
{
#if USE_RAY_QUERY
    uint dispatchIdx = idx;
#else
    uint dispatchIdx = DispatchRaysIndex().x;
#endif

    uint activeCount = CounterBuffer[0];
    if (activeCount == 0 || dispatchIdx >= activeCount)
        return;

    // Direct sequential streaming read of sorted ray records across the wave
    RayRecord record = RayRecords[dispatchIdx];

    uint2 pixelCoord = (uint2)record.PixelCoord;
    if (any(pixelCoord >= Camera.RenderSize))
        return;

    float3 rayDirection = UnpackDirectionOctahedral(record.OctahedralDirection);
    half3 throughput = record.Throughput;

    RayDesc ray;
    ray.Origin = record.Origin;
    ray.Direction = rayDirection;
    ray.TMin = 0.0f;
    ray.TMax = RAY_TMAX;

    uint randomSeed = record.RandomSeed;
#if USE_RAY_QUERY
    Payload payload = TraceRayStandard(Scene, ray, randomSeed);
#else
    Payload payload;
    payload.Init(randomSeed);
    TraceRay(Scene, RAY_FLAGS, INSTANCE_MASK, DIFFUSE_RAY_HITGROUP_IDX, 0, DIFFUSE_RAY_MISS_IDX, ray, payload);
    randomSeed = payload.randomSeed;
#endif

    half3 sampleRadiance = half3(0.0h, 0.0h, 0.0h);

    if (!payload.Hit())
    {
        half3 skyIrradiance = (half3)(SampleSky(SkyHemisphere, ray.Direction) * Raytracing.Sky);
        sampleRadiance = skyIrradiance * throughput;
    }
    else
    {
        float3 localPosition = ray.Origin + ray.Direction * payload.hitDistance;

        RayCone rayCone = RayCone::make(Raytracing.PixelConeSpreadAngle * payload.hitDistance, Raytracing.PixelConeSpreadAngle);

        Instance instance;
        LightingMaterialData materialData;
        Surface surface = SurfaceMaker::make(localPosition, payload, ray.Direction, rayCone, instance, materialData, false);

        BRDFContext brdfContext = BRDFContext::make(surface, -ray.Direction);
        const bool isEnter = dot(surface.FaceNormal, brdfContext.ViewDirection) >= 0.0f;
        if (!isEnter)
        {
            surface.FlipNormal();
            brdfContext.NdotV = saturate(dot(surface.Normal, brdfContext.ViewDirection));
        }

        AdjustShadingNormal(surface, brdfContext, true, false);
        StandardBSDF bsdf = StandardBSDF::make(surface, surface.Normal, brdfContext.ViewDirection, isEnter);

        const half3 directRadiance = (half3)EvaluateDirectRadiance(materialData.Type, materialData.Feature, surface, brdfContext, instance, bsdf, randomSeed, surface.Primary);
        sampleRadiance = (directRadiance + (half3)surface.Emissive) * throughput;
    }

    sampleRadiance = clamp(sampleRadiance, 0.0h, 65504.0h);
    Output[pixelCoord].xyz += sampleRadiance;
}
