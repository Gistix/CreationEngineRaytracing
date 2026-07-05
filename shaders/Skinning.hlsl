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

float3x3 ExtractNormalizedRotation(float3x4 transform)
{
    float3 x = normalize(transform[0].xyz);
    float3 y = normalize(transform[1].xyz);

    // Remove any component of Y along X
    y = normalize(y - x * dot(x, y));

    // Rebuild Z
    float3 z = cross(x, y);

    return float3x3(x, y, z);
}

float4 MatrixToQuaternion(float3x3 m)
{
    float4 q;

    float trace = m[0][0] + m[1][1] + m[2][2];

    if (trace > 0.0)
    {
        float s = sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (m[2][1] - m[1][2]) / s;
        q.y = (m[0][2] - m[2][0]) / s;
        q.z = (m[1][0] - m[0][1]) / s;
    }
    else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
    {
        float s = sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    }
    else if (m[1][1] > m[2][2])
    {
        float s = sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (m[1][2] + m[2][1]) / s;
    }
    else
    {
        float s = sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25 * s;
    }

    return normalize(q);
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

    const bool isDynamic = (updateData.meshFlags & Skinning::MeshFlags::Dynamic) != 0;
    const bool isMSN = (updateData.meshFlags & Skinning::MeshFlags::ModelSpaceNormal) != 0;
    const bool doSkin = (updateData.updateFlags & Skinning::DirtyFlags::Skin) != 0;

	VertexDesc vertexDesc = updateData.VertexDesc;

    const uint vertexSize = (uint) vertexDesc.GetVertexSize();
    const uint vertexOffset = vertexSize * vertexIndex;
	
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

    RWByteAddressBuffer output = OutputVertices[slot];
	
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
        const float3x3 boneRot = ExtractNormalizedRotation(boneMatrix);

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
		
		// Write quaternion
        if (isMSN)
        {
			// Positioned after the original vertices
            const uint quatOffset = (vertexSize * updateData.vertexCount) + vertexIndex * 8u;
			
            float4 q = MatrixToQuaternion(boneRot);
			
            uint2 packed;
            packed.x = f32tof16(q.x) | (f32tof16(q.y) << 16);
            packed.y = f32tof16(q.z) | (f32tof16(q.w) << 16);
			
            output.Store2(quatOffset, packed);
        }
    }

	// Save the live position as the previous frame's position before overwriting (motion vectors).
	PrevPositions[slot][vertexIndex] = asfloat(output.Load3(posOffset));

	// Position (xyz) is always refreshed; normal/tangent/bitangent only when the layout has them.
    if (hasPosition)
        output.Store3(posOffset, asuint(position4.xyz));
	else if(isDynamic)
    {
        // Save last frame's skinned position into the previous-position region (stored after the
        // current set) before refreshing the current position for this frame.
        DynamicVerticesOut[dynSlot][vertexIndex + updateData.vertexCount] = DynamicVerticesOut[dynSlot][vertexIndex];
        DynamicVerticesOut[dynSlot][vertexIndex] = position4;
    }
		
	if (hasNormal)
	{
		if (hasTangent)
		{
			// tangent.x is packed into pos.w, which only exists when the layout has a position attribute.
			// No-position meshes (e.g. dynamic mesh) reconstruct tangent.x on read, so don't write it here.
			if (hasPosition)
				output.Store(posOffset + 12, asuint(T.x));             // tangent.x

			output.Store(normOffset, PackByte4SNorm(float4(N, T.y)));   // normal.xyz + tangent.y
			output.Store(binormOffset, PackByte4SNorm(float4(B, T.z))); // bitangent.xyz + tangent.z
		}
		else
		{
			output.Store(normOffset, PackByte4SNorm(float4(N, normal4.w))); // normal.xyz, preserve w
		}
	}
}
