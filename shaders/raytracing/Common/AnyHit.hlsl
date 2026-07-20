#include "raytracing/Pathtracing/Registers.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Transparency.hlsli"

[shader("anyhit")]
void Main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    if (!ConsiderTransparentMaterial(
                InstanceID(),
                GeometryIndex(),
                PrimitiveIndex(),
                attribs.barycentrics,
                payload.randomSeed))
        IgnoreHit();
}

