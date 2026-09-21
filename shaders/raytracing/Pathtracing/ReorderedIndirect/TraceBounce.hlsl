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
    uint2 dims = DispatchRaysDimensions().xy;
    uint2 index2D = DispatchRaysIndex().xy;
    uint dispatchIdx = index2D.y * dims.x + index2D.x;
#endif

    uint activeCount = CounterBuffer[0];
    if (activeCount == 0 || dispatchIdx >= activeCount)
        return;

    uint rayIdx = SortedRayIndices[dispatchIdx];
    RayRecord record = RayRecords[rayIdx];

    uint2 pixelCoord = uint2(record.PixelCoord & 0xFFFF, record.PixelCoord >> 16);
    if (any(pixelCoord >= Camera.RenderSize))
        return;

    RayDesc ray;
    ray.Origin = record.Origin;
    ray.Direction = record.Direction;
    ray.TMin = 0.0f;
    ray.TMax = RAY_TMAX;

    uint randomSeed = record.RandomSeed;
    Payload payload = TraceRayStandard(Scene, ray, randomSeed);

    half3 sampleRadiance = half3(0.0h, 0.0h, 0.0h);

    if (!payload.Hit())
    {
        half3 skyIrradiance = (half3)(SampleSky(SkyHemisphere, ray.Direction) * Raytracing.Sky);
        sampleRadiance = skyIrradiance * (half3)record.Throughput;
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
        sampleRadiance = (directRadiance + (half3)surface.Emissive) * (half3)record.Throughput;
    }

    sampleRadiance = clamp(sampleRadiance, 0.0h, 65504.0h);
    Output[pixelCoord].xyz += sampleRadiance;
}
