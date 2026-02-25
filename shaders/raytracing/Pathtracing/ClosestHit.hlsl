#include "raytracing/include/Payload.hlsli"

[shader("closesthit")]
void Main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.hitDistance = RayTCurrent();
    payload.primitiveIndex = PrimitiveIndex();
    payload.PackBarycentrics(attribs.barycentrics);
    payload.PackInstanceGeometryIndex(InstanceIndex(), GeometryIndex());
}