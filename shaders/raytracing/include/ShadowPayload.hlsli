#ifndef SHADOWPAYLOAD_HLSLI
#define SHADOWPAYLOAD_HLSLI

struct ShadowPayload
{
    float hitDistance;
    float3 transmission;
    uint randomSeed;

    bool Hit() { return hitDistance > 0.0f; }
};

#endif // SHADOWPAYLOAD_HLSLI