#include "raytracing/include/ShadowPayload.hlsli"

[shader("miss")]
void Main(inout ShadowPayload payload)
{
    payload.hitDistance = -1.0f;
}
