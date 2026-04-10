#include "interop/Micro/DisplacementMM.hlsli"
#include "interop/Vertex.hlsli"
#include "interop/Triangle.hlsli"

ConstantBuffer<DisplacementMMData> DMM   : register(b0);

StructuredBuffer<Triangle> Triangles[]   : register(t0, space1);
StructuredBuffer<Vertex>   Vertices[]    : register(t0, space2);
Texture2D<float4>          Textures[]    : register(t0, space3);
SamplerState               LinearRepeat  : register(s0);

// OUTPUTS
RWStructuredBuffer<float>  MicroValues     : register(u0); // temp (pre-normalized)
RWStructuredBuffer<float2> BiasScale       : register(u1); // per triangle
RWByteAddressBuffer        PackedMicromaps : register(u2); // final output (bit-packed)

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

uint ComputeMicroVertexCount(uint N)
{
    return (N + 1) * (N + 2) / 2;
}

// Convert linear index → barycentric (u,v)
void DecodeBarycentric(uint i, uint N, out uint u, out uint v)
{
    uint idx = 0;

    [loop]
    for (uint row = 0; row <= N; row++)
    {
        uint rowCount = N - row + 1;
        if (i < idx + rowCount)
        {
            u = row;
            v = i - idx;
            return;
        }
        idx += rowCount;
    }

    u = 0;
    v = 0;
}

// Pack 11-bit value into RWByteAddressBuffer
void Pack11Bits(uint baseOffsetBytes, uint bitIndex, uint value)
{
    uint bitOffset = bitIndex * 11;
    uint wordIndex = bitOffset >> 5;          // /32
    uint bitInWord = bitOffset & 31;

    uint addr0 = baseOffsetBytes + wordIndex * 4;
    uint addr1 = baseOffsetBytes + (wordIndex + 1) * 4;

    uint v = value & 0x7FF;

    uint old0 = PackedMicromaps.Load(addr0);
    uint new0 = old0 | (v << bitInWord);
    PackedMicromaps.Store(addr0, new0);

    if (bitInWord > 21) // spills into next word
    {
        uint spillBits = 32 - bitInWord;
        uint old1 = PackedMicromaps.Load(addr1);
        uint new1 = old1 | (v >> spillBits);
        PackedMicromaps.Store(addr1, new1);
    }
}

// ------------------------------------------------------------
// PASS 1: Generate micro displacement
// ------------------------------------------------------------

[numthreads(64,1,1)]
void CS_Generate(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint triIndex = gid.x;
    if (triIndex >= DMM.TriangleCount) return;

    const uint meshIndex = DMM.MeshIndex;
    
    Triangle tri = Triangles[meshIndex][triIndex];

    Vertex v0 = Vertices[meshIndex][tri.x];
    Vertex v1 = Vertices[meshIndex][tri.y];
    Vertex v2 = Vertices[meshIndex][tri.z];

    uint N = DMM.SubdivisionLevel;
    uint total = ComputeMicroVertexCount(N);

    for (uint i = tid.x; i < total; i += 64)
    {
        uint u, v;
        DecodeBarycentric(i, N, u, v);

        float fu = (float)u / N;
        float fv = (float)v / N;
        float fw = 1.0 - fu - fv;

        float2 uv =
            fw * v0.Texcoord0 +
            fu * v1.Texcoord0 +
            fv * v2.Texcoord0;

        float h = Textures[DMM.DisplacementIndex].SampleLevel(LinearRepeat, uv, 0).r;

        MicroValues[triIndex * total + i] = h;
    }
}

// ------------------------------------------------------------
// PASS 2: Min/Max reduction
// ------------------------------------------------------------

groupshared float localMin[64];
groupshared float localMax[64];

[numthreads(64,1,1)]
void CS_MinMax(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint triIndex = gid.x;
    if (triIndex >= DMM.TriangleCount) return;

    uint N = DMM.SubdivisionLevel;
    uint total = (N + 1) * (N + 2) / 2;

    float minVal = 1e30;
    float maxVal = -1e30;

    for (uint i = tid.x; i < total; i += 64)
    {
        float v = MicroValues[triIndex * total + i];
        minVal = min(minVal, v);
        maxVal = max(maxVal, v);
    }

    localMin[tid.x] = minVal;
    localMax[tid.x] = maxVal;

    GroupMemoryBarrierWithGroupSync();

    // reduction
    for (uint stride = 32; stride > 0; stride >>= 1)
    {
        if (tid.x < stride)
        {
            localMin[tid.x] = min(localMin[tid.x], localMin[tid.x + stride]);
            localMax[tid.x] = max(localMax[tid.x], localMax[tid.x + stride]);
        }

        GroupMemoryBarrierWithGroupSync();
    }

    if (tid.x == 0)
    {
        float minV = localMin[0];
        float maxV = localMax[0];

        float scale = max(maxV - minV, 1e-6);

        BiasScale[triIndex] = float2(minV, scale);
    }
}

// ------------------------------------------------------------
// PASS 3: Normalize + NV-style 11-bit packing
// ------------------------------------------------------------

[numthreads(64,1,1)]
void CS_NormalizePack(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID)
{
    uint triIndex = gid.x;
    if (triIndex >= DMM.TriangleCount) return;

    uint N = DMM.SubdivisionLevel;
    uint total = ComputeMicroVertexCount(N);

    float2 bs = BiasScale[triIndex];
    float bias  = bs.x;
    float scale = bs.y;

    uint baseOffset = triIndex * DMM.PackedStride;

    for (uint i = tid.x; i < total; i += 64)
    {
        float h = MicroValues[triIndex * total + i];
        float norm = saturate((h - bias) / scale);

        // 11-bit quantization (NV formats use 11-bit typically)
        uint q = (uint)(norm * 2047.0 + 0.5);

        Pack11Bits(baseOffset, i, q);
    }
}