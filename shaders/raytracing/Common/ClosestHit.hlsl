#include "raytracing/include/Payload.hlsli"

[shader("closesthit")]
void Main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attribs)
{
    payload.PackAll(attribs);
}