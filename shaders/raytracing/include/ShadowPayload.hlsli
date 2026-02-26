#ifndef SHADOWPAYLOAD_HLSLI
#define SHADOWPAYLOAD_HLSLI

struct ShadowPayload
{
    float missed;
    float3 transmission;
    uint randomSeed;
};

#endif // SHADOWPAYLOAD_HLSLI