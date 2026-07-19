#include "raytracing/Pathtracing/Registers.hlsli"
#include "raytracing/include/ShadowPayload.hlsli"
#include "raytracing/include/Geometry.hlsli"
#include "include/Surface.hlsli"
#include "raytracing/include/Transparency.hlsli"

[shader("anyhit")]
void Main(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    bool committed = ConsiderTransparentMaterialShadow(
                InstanceID(),
                GeometryIndex(),
                PrimitiveIndex(),
                attribs.barycentrics,
                payload.randomSeed,
                WorldRayDirection(),
                RayTCurrent(),
                payload.transmission);

    // Fully occluded: accept this hit to terminate the search immediately.
    // The result is ~zero shadow either way (transmission * missed=0).
    // Must run before IgnoreHit(), which would end the invocation.
    if (all(payload.transmission <= 1e-3f))
        AcceptHitAndEndSearch();

    if (!committed)
        IgnoreHit();
}
