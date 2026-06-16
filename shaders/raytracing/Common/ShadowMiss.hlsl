#include "raytracing/include/ShadowPayload.hlsli"

[shader("miss")]
void Main(inout ShadowPayload payload)
{
    payload.missed = 1.0f;
}
