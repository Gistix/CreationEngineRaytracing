#include "raytracing/Pathtracing/Registers.hlsli"
#include "raytracing/include/Payload.hlsli"
#include "raytracing/include/Transparency.hlsli"

[shader("anyhit")]
void Main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    if (!ConsiderTransparentMaterial(
                InstanceIndex(),
                GeometryIndex(),
                PrimitiveIndex(),
                attribs.barycentrics,
                payload.randomSeed))
        IgnoreHit();
}

