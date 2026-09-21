#ifndef RAY_RECORD_HLSLI
#define RAY_RECORD_HLSLI

struct RayRecord
{
    float3    Origin;              // 12 bytes
    uint16_t2 PixelCoord;          // 4 bytes: [0]=x, [1]=y
    half2     OctahedralDirection; // 4 bytes: 2D octahedral projection in [-1.0, 1.0]
    uint      RandomSeed;          // 4 bytes
    half3     Throughput;          // 6 bytes
    half      Pad;                 // 2 bytes
};

inline half2 PackDirectionOctahedral(float3 dir)
{
    float l1 = abs(dir.x) + abs(dir.y) + abs(dir.z);
    float3 p = (l1 > 1e-6f) ? (dir / l1) : float3(0.0f, 0.0f, 1.0f);
    float2 s = select(p.xy >= 0.0f, float2(1.0f, 1.0f), float2(-1.0f, -1.0f));
    float2 oct = (p.z >= 0.0f) ? p.xy : ((1.0f - abs(p.yx)) * s);
    return (half2)oct;
}

inline float3 UnpackDirectionOctahedral(half2 octHalf)
{
    float2 oct = (float2)octHalf;
    float3 p = float3(oct.x, oct.y, 1.0f - abs(oct.x) - abs(oct.y));
    if (p.z < 0.0f)
    {
        float2 s = select(p.xy >= 0.0f, float2(1.0f, 1.0f), float2(-1.0f, -1.0f));
        p.xy = (1.0f - abs(p.yx)) * s;
    }
    return normalize(p);
}

#endif // RAY_RECORD_HLSLI
