#if defined(GLOBAL_ILLUMINATION)
#   include "raytracing/GlobalIllumination/Registers.hlsli"
#elif defined(GBUFFER)
#   include "raytracing/GBuffer/Registers.hlsli"
#elif defined(DEBUG)
#   include "raytracing/Debug/Registers.hlsli"
#else
#   include "raytracing/Pathtracing/Registers.hlsli"
#endif
#include "raytracing/include/ShadowPayload.hlsli"
#include "raytracing/include/Geometry.hlsli"
#include "include/Surface.hlsli"
#include "raytracing/include/Transparency.hlsli"

[shader("anyhit")]
void Main(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    if (!ConsiderTransparentMaterialShadow(
                InstanceID(),
                GeometryIndex(),
                PrimitiveIndex(),
                attribs.barycentrics,
                payload.randomSeed,
                WorldRayDirection(),
                RayTCurrent(),
                payload.transmission))
        IgnoreHit();
}
