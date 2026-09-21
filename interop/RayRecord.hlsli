#ifndef RAY_RECORD_HLSLI
#define RAY_RECORD_HLSLI

struct RayRecord
{
    float3 Origin;
    uint   PixelCoord; // (y << 16) | x
    float3 Direction;
    uint   RandomSeed;
    float3 Throughput;
    float  Pad;
};

#endif // RAY_RECORD_HLSLI
