#if defined(GLOBAL_ILLUMINATION)
#   include "raytracing/GlobalIllumination/Registers.hlsli"
#elif defined(GBUFFER)
#   include "raytracing/GBuffer/Registers.hlsli"
#elif defined(DEBUG)
#   include "raytracing/Debug/Registers.hlsli"
#else
#   include "raytracing/Pathtracing/Registers.hlsli"
#endif
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

