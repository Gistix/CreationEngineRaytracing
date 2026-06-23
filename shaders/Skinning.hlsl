#include "Interop/Vertex.hlsli"
#include "Interop/VertexUpdate.hlsli"
#include "Interop/BoneMatrix.hlsli"
#include "Interop/Mesh.hlsli"
#include "Interop/VertexDesc.hlsli"

StructuredBuffer<VertexUpdateData> UpdateData           : register(t0);
StructuredBuffer<BoneMatrix> BoneMatrices               : register(t1);

// Dynamic float4 positions (input). Lives in DynamicMesh, addressed by updateData.dynamicIndex.
StructuredBuffer<float4> DynamicVertices[]             : register(t0, space1);
// Original (rest-pose) vertices in native packed format; also carries inline skinning. Shared slot.
ByteAddressBuffer OriginalVertices[]                   : register(t0, space2);

// Live (output) vertices in native packed format; read by the RT path. Shared slot.
RWByteAddressBuffer OutputVertices[]                   : register(u0);
// Previous skinned positions for motion vectors. Shared slot.
RWStructuredBuffer<float3> PrevPositions[]             : register(u0, space1);
// Dynamic float4 positions (output). Lives in DynamicMesh, addressed by updateData.dynamicIndex.
RWStructuredBuffer<float4> DynamicVerticesOut[]        : register(u0, space2);

// Decodes a signed-normalized byte4 (ubyte4 * 2 - 1) from a raw uint.
float4 UnpackByte4SNorm(uint packed)
{
	const float4 v = float4(
		(float)((packed >> 0) & 0xFF),
		(float)((packed >> 8) & 0xFF),
		(float)((packed >> 16) & 0xFF),
		(float)((packed >> 24) & 0xFF));
	return v * (1.0f / 255.0f) * 2.0f - 1.0f;
}

// Encodes a signed-normalized float4 back into a packed ubyte4 (inverse of UnpackByte4SNorm).
uint PackByte4SNorm(float4 v)
{
	const float4 s = saturate(v * 0.5f + 0.5f) * 255.0f + 0.5f;
	const uint4 b = (uint4)s;
	return (b.x) | (b.y << 8) | (b.z << 16) | (b.w << 24);
}

float3x4 GetBoneTransformMatrix(uint4 bones, float4 weights, uint boneOffset)
{
	float3x4 m = BoneMatrices[boneOffset + bones.x].World * weights.x;
	m += BoneMatrices[boneOffset + bones.y].World * weights.y;
	m += BoneMatrices[boneOffset + bones.z].World * weights.z;
	m += BoneMatrices[boneOffset + bones.w].World * weights.w;
	return m;
}

[numthreads(1, 32, 1)]
void Main(uint2 DTid : SV_DispatchThreadID)
{
	const uint meshIndex = DTid.x;
	const uint vertexIndex = DTid.y;

	VertexUpdateData updateData = UpdateData[meshIndex];

	if (vertexIndex >= updateData.vertexCount)
		return;

	const uint slot = NonUniformResourceIndex(updateData.index);
	const uint dynSlot = NonUniformResourceIndex(updateData.dynamicIndex);

	const bool isDynamic = (updateData.shapeFlags & MeshFlags::Dynamic) != 0;
	const bool doSkin = (updateData.updateFlags & DirtyFlags::Skin) != 0;

	VertexDesc vertexDesc = updateData.VertexDesc;

	const uint vertexOffset = (uint)vertexDesc.GetVertexSize() * vertexIndex;
	
	const uint posOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Position);
    uint normOffset = 0;
    uint binormOffset = 0;
	
    const bool hasPosition = vertexDesc.HasFlag(VertexFlags::Vertex);
	const bool hasNormal = vertexDesc.HasFlag(VertexFlags::Normal);
	const bool hasTangent = hasNormal && vertexDesc.HasFlag(VertexFlags::Tangent);

	ByteAddressBuffer original = OriginalVertices[slot];

	// Position (float4; w carries tangent.x).
    float4 position4 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 normal4 = float4(0.0f, 0.0f, 1.0f, 0.0f);
    
    if (hasPosition)
        position4 = asfloat(original.Load4(posOffset));
    else if (isDynamic)
        position4 = DynamicVertices[dynSlot][vertexIndex];

	float3 N = float3(0.0f, 0.0f, 1.0f);
	float3 T = float3(1.0f, 0.0f, 0.0f);
	float3 B = float3(0.0f, 1.0f, 0.0f);

	if (hasNormal)
	{
		// Normal (byte4 snorm; w carries tangent.y).
		normOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Normal);
		normal4 = UnpackByte4SNorm(original.Load(normOffset));
		N = normalize(normal4.xyz);

		if (hasTangent)
		{
			// Binormal (byte4 snorm; w carries tangent.z); tangent split across pos.w/normal.w/binormal.w.
			binormOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Binormal);
			const float4 binorm4 = UnpackByte4SNorm(original.Load(binormOffset));
			B = binorm4.xyz;
            T = float3(position4.w, normal4.w, binorm4.w);
        }
	}

	if (doSkin)
	{
		// Inline skinning (assumes 4 bones/vertex): 4 half weights, then 4 uint8 bone ids.
		const uint skinOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Skinning);

		const uint2 weightsPacked = original.Load2(skinOffset);
		float4 weights = float4(
			f16tof32(weightsPacked.x & 0xFFFF), f16tof32(weightsPacked.x >> 16),
			f16tof32(weightsPacked.y & 0xFFFF), f16tof32(weightsPacked.y >> 16));

		const uint bonesPacked = original.Load(skinOffset + 8);
		const uint4 bones = uint4(
			bonesPacked & 0xFF, (bonesPacked >> 8) & 0xFF,
			(bonesPacked >> 16) & 0xFF, (bonesPacked >> 24) & 0xFF);

		const uint maxBone = max(max(bones.x, bones.y), max(bones.z, bones.w));
		if (maxBone >= updateData.numMatrices)
			return;

		const float3x4 boneMatrix = GetBoneTransformMatrix(bones, weights, updateData.boneOffset);
		const float3x3 boneRot = (float3x3)boneMatrix;

        if (hasPosition || isDynamic)
			position4.xyz = mul(boneMatrix, float4(position4.xyz, 1.0f));

		if (hasNormal)
		{
			N = normalize(mul(boneRot, N));
			if (hasTangent)
			{
				T = normalize(mul(boneRot, T));
				B = normalize(mul(boneRot, B));
            }
		}
	}

	RWByteAddressBuffer output = OutputVertices[slot];

	// Save the live position as the previous frame's position before overwriting (motion vectors).
	PrevPositions[slot][vertexIndex] = asfloat(output.Load3(posOffset));

	// Position (xyz) is always refreshed; normal/tangent/bitangent only when the layout has them.
    if (hasPosition)
        output.Store3(posOffset, asuint(position4.xyz));
	else if(isDynamic)
        DynamicVerticesOut[dynSlot][vertexIndex] = position4;
		
	if (hasNormal)
	{
		if (hasTangent)
		{
			output.Store(posOffset + 12, asuint(T.x));                  // tangent.x
			output.Store(normOffset, PackByte4SNorm(float4(N, T.y)));   // normal.xyz + tangent.y
			output.Store(binormOffset, PackByte4SNorm(float4(B, T.z))); // bitangent.xyz + tangent.z
		}
		else
		{
			output.Store(normOffset, PackByte4SNorm(float4(N, normal4.w))); // normal.xyz, preserve w
		}
	}
}
