#include "Interop/Vertex.hlsli"
#include "Interop/LandLODUpdate.hlsli"
#include "include/WaveSize.hlsli"

struct PushContants
{
    float4 HighDetailRange;
};

#if defined(__spirv__)
[[vk::push_constant]] ConstantBuffer<PushContants> PC : register(b0);
#else
ConstantBuffer<PushContants> PC             : register(b0);
#endif

StructuredBuffer<LandLODUpdate> UpdateData  : register(t0);

#if defined(__spirv__)
[[vk::binding(0, 1)]] ByteAddressBuffer Vertices[];
[[vk::binding(0, 2)]] RWByteAddressBuffer OutputVertices[];
#else
ByteAddressBuffer Vertices[]                : register(t0, space2);
RWByteAddressBuffer OutputVertices[]        : register(u0);
#endif

float3 AdjustLodLandscapeVertexPositionMS(float3 positionMS, float3 positionWS, float4 cellParams)
{
    float worldXShift = positionWS.x - cellParams.x;
    float worldYShift = positionWS.y - cellParams.y;
    if ((abs(worldXShift) < cellParams.z) && (abs(worldYShift) < cellParams.w))
    {
        positionMS.z -= (230 + positionWS.z / 1e9);
    }
    return positionMS;
}

WAVE_SIZE(32)
[numthreads(1, 32, 1)]
void Main(uint2 DTid : SV_DispatchThreadID)
{
    const uint meshIndex = DTid.x;
    const uint vertexIndex = DTid.y;

    LandLODUpdate updateData = UpdateData[meshIndex];

    if (vertexIndex >= updateData.VertexCount)
        return;

    uint shapeIndex = NonUniformResourceIndex(updateData.Index);
    uint addr = vertexIndex * updateData.VertexStride;

    float3 positionMS = float3(
        asfloat(Vertices[shapeIndex].Load(addr)),
        asfloat(Vertices[shapeIndex].Load(addr + 4)),
        asfloat(Vertices[shapeIndex].Load(addr + 8)));
    
    float3 positionWS = mul(updateData.Transform, float4(positionMS, 1.0));

    positionMS = AdjustLodLandscapeVertexPositionMS(positionMS, positionWS, PC.HighDetailRange);
    
    OutputVertices[shapeIndex].Store(addr, asuint(positionMS.x));
    OutputVertices[shapeIndex].Store(addr + 4, asuint(positionMS.y));
    OutputVertices[shapeIndex].Store(addr + 8, asuint(positionMS.z));
}