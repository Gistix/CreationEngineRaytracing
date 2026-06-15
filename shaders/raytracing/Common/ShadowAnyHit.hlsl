#include "raytracing/Pathtracing/Registers.hlsli"
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
