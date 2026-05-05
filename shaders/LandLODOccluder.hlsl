#include "Interop/Vertex.hlsli"
#include "Interop/LandLODUpdate.hlsli"

struct PushContants
{
    float4 HighDetailRange;
};

ConstantBuffer<PushContants> PC             : register(b0);

StructuredBuffer<LandLODUpdate> UpdateData  : register(t0);

StructuredBuffer<Vertex> Vertices[]         : register(t0, space2);
RWStructuredBuffer<Vertex> OutputVertices[] : register(u0);

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

[numthreads(1, 32, 1)]
void Main(uint2 DTid : SV_DispatchThreadID)
{
    const uint meshIndex = DTid.x;
    const uint vertexIndex = DTid.y;

    LandLODUpdate updateData = UpdateData[meshIndex];

    if (vertexIndex >= updateData.VertexCount)
        return;

    uint shapeIndex = NonUniformResourceIndex(updateData.Index);

    Vertex vertex = Vertices[shapeIndex][vertexIndex];
    
    float3 positionRS = mul(updateData.MeshTransform, float4(vertex.Position, 1.0));
    float3 positionWS = mul(updateData.InstanceTransform, float4(positionRS, 1.0));
    
    vertex.Position = AdjustLodLandscapeVertexPositionMS(vertex.Position, positionWS, PC.HighDetailRange);
    
    OutputVertices[shapeIndex][vertexIndex] = vertex;
}