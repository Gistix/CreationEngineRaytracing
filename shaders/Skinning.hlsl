#include "Interop/Vertex.hlsli"
#include "Interop/VertexUpdate.hlsli"
#include "Interop/Skinning.hlsli"
#include "Interop/BoneMatrix.hlsli"

StructuredBuffer<VertexUpdateData> UpdateData           : register(t0);
StructuredBuffer<BoneMatrix> BoneMatrices               : register(t1);

StructuredBuffer<float4> DynamicVertices[]              : register(t0, space1);
StructuredBuffer<Vertex> Vertices[]                     : register(t0, space2);
StructuredBuffer<Skinning> MeshSkinning[]               : register(t0, space3);

RWStructuredBuffer<Vertex> OutputVertices[]             : register(u0);

namespace MeshFlags
{
    static const uint Dynamic = (1 << 1);
    static const uint Skinned = (1 << 2);
}

namespace DirtyFlags
{
    static const uint Skin = (1 << 1);    
    static const uint Vertex = (1 << 2);
}

float3x4 GetBoneTransformMatrix(Skinning skinning, uint boneOffset)
{
	float3x4 boneMatrix1 = BoneMatrices[boneOffset + skinning.GetBone(0)].World;
	float3x4 boneMatrix2 = BoneMatrices[boneOffset + skinning.GetBone(1)].World;
	float3x4 boneMatrix3 = BoneMatrices[boneOffset + skinning.GetBone(2)].World;
	float3x4 boneMatrix4 = BoneMatrices[boneOffset + skinning.GetBone(3)].World;

	return boneMatrix1 * skinning.weight[0] +
		   boneMatrix2 * skinning.weight[1] +
		   boneMatrix3 * skinning.weight[2] +
		   boneMatrix4 * skinning.weight[3];
}

float3x3 GetBoneRSMatrix(Skinning skinning, uint boneOffset)
{
    float3x4 boneMatrix1 = BoneMatrices[boneOffset + skinning.GetBone(0)].World;
    float3x4 boneMatrix2 = BoneMatrices[boneOffset + skinning.GetBone(1)].World;
    float3x4 boneMatrix3 = BoneMatrices[boneOffset + skinning.GetBone(2)].World;
    float3x4 boneMatrix4 = BoneMatrices[boneOffset + skinning.GetBone(3)].World;

    float3x3 rs1 = (float3x3)boneMatrix1;
    float3x3 rs2 = (float3x3)boneMatrix2;
    float3x3 rs3 = (float3x3)boneMatrix3;
    float3x3 rs4 = (float3x3)boneMatrix4;

    return rs1 * skinning.weight[0] +
           rs2 * skinning.weight[1] +
           rs3 * skinning.weight[2] +
           rs4 * skinning.weight[3];
}

[numthreads(1, 32, 1)]
void Main(uint2 DTid : SV_DispatchThreadID)
{
    const uint meshIndex = DTid.x;
    const uint vertexIndex = DTid.y;

    VertexUpdateData updateData = UpdateData[meshIndex];

    if (vertexIndex >= updateData.vertexCount)
        return;

    uint shapeIndex = NonUniformResourceIndex(updateData.index);

    Vertex vertex = Vertices[shapeIndex][vertexIndex];

    float3 position = vertex.Position;
    
    // Always fetch dynamic positions for dynamic shapes
    if (updateData.shapeFlags & MeshFlags::Dynamic)
        position = DynamicVertices[shapeIndex][vertexIndex].xyz;

    if (updateData.updateFlags & DirtyFlags::Skin)
    {
        Skinning skinning = MeshSkinning[shapeIndex][vertexIndex];
        
        uint maxBone = max(skinning.GetBone(0), max(skinning.GetBone(1), max(skinning.GetBone(2), skinning.GetBone(3))));
        
        if (maxBone >= updateData.numMatrices)
            return;     
        
        float3x4 boneMatrix = GetBoneTransformMatrix(skinning, updateData.boneOffset);

        position = mul(boneMatrix, float4(position, 1.0f));

        float3x3 boneMatrixRot = (float3x3)boneMatrix;

        vertex.Normal = (half3) normalize(mul(boneMatrixRot, vertex.Normal));
        vertex.Tangent = (half3) normalize(mul(boneMatrixRot, vertex.Tangent));
    }

    vertex.Position = position;
    
    OutputVertices[shapeIndex][vertexIndex] = vertex;
}