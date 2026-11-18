#include "Interop/VertexAnimation.hlsli"
#include "Interop/VertexDesc.hlsli"
#include "include/WaveSize.hlsli"
#include "include/Vulkan.hlsli"

StructuredBuffer<VertexAnimationData> AnimationData : register(t0);

// Original (rest-pose) vertices in native packed format. Shared slot.
VK_BINDING(1, 0) ByteAddressBuffer OriginalVertices[]                 : register(t0, space2);
// CPU-uploaded dynamic float4 positions (BSDynamicTriShape morphs). Dynamic slot.
VK_BINDING(2, 0) StructuredBuffer<float4> DynamicVertices[]           : register(t0, space1);
// Pre-skin animated positions consumed by the skinning pass. Shared slot.
VK_BINDING(3, 0) RWStructuredBuffer<float4> AnimatedPositions[]       : register(u0, space3);
// Live (output) vertices in native packed format for non-skinned meshes. Shared slot.
VK_BINDING(4, 0) RWByteAddressBuffer OutputVertices[]                 : register(u0);
// Previous positions for motion vectors (non-skinned meshes). Shared slot.
VK_BINDING(5, 0) RWStructuredBuffer<float3> PrevPositions[]           : register(u0, space1);

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

// Matches vanilla Lighting.hlsl GetTreeShiftVector (current-frame component only).
// params.x = leaf frequency * wind magnitude, params.y = leaf amplitude, params.z = timer.
float GetTreeShift(float3 position, float colorAlpha, float4 params)
{
	precise float2 tmp1 = (params.x * params.z).xx;
	precise float2 tmp2 = float2(0.1f, 0.25f) * tmp1 + dot(position, 1.0f.xxx).xx;
	precise float2 tmp3 = abs(-1.0f.xx + 2.0f.xx * frac(0.5f.xx + tmp2));
	precise float2 tmp4 = (tmp3 * tmp3) * (3.0f.xx - 2.0f.xx * tmp3);
	return (tmp4.x + 0.1f * tmp4.y) * (params.y * colorAlpha);
}

WAVE_SIZE(32)
[numthreads(1, 32, 1)]
void Main(uint2 DTid : SV_DispatchThreadID)
{
	const uint meshIndex = DTid.x;
	const uint vertexIndex = DTid.y;

	const VertexAnimationData data = AnimationData[meshIndex];

	if (vertexIndex >= data.VertexCount)
		return;

	const uint slot = NonUniformResourceIndex(data.Index);

	const bool treeAnim = (data.Flags & VertexAnimation::Flags::TreeAnim) != 0;
	const bool dynamicSource = (data.Flags & VertexAnimation::Flags::DynamicSource) != 0;
	const bool skinnedOutput = (data.Flags & VertexAnimation::Flags::SkinnedOutput) != 0;

	VertexDesc vertexDesc = data.VertexDesc;

	const uint vertexSize = (uint)vertexDesc.GetVertexSize();
	const uint vertexOffset = vertexSize * vertexIndex;
	const uint posOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Position);
	const bool hasPosition = vertexDesc.HasFlag(VertexFlags::Vertex);
	const bool fullPrecision = vertexDesc.HasFlag(VertexFlags::FullPrec);

	ByteAddressBuffer original = OriginalVertices[slot];

	// Rest-pose position; w carries tangent.x for packed layouts and is preserved on write.
	float4 position4 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	if (hasPosition)
	{
		if (fullPrecision)
		{
			position4 = asfloat(original.Load4(posOffset));
		}
		else
		{
			const uint2 packed = original.Load2(posOffset);
			position4 = float4(
				f16tof32(packed.x & 0xFFFF), f16tof32(packed.x >> 16),
				f16tof32(packed.y & 0xFFFF), f16tof32(packed.y >> 16));
		}
	}

	// Morph positions replace the rest pose; keep w when the native layout has no position to carry it.
	float sourceW = 1.0f;
	if (dynamicSource)
	{
		const float4 dynamicPosition = DynamicVertices[NonUniformResourceIndex(data.DynamicIndex)][vertexIndex];
		position4.xyz = dynamicPosition.xyz;
		sourceW = dynamicPosition.w;
	}

	float3 animated = position4.xyz;

	if (treeAnim)
	{
		// Vanilla uses the raw unpacked normal (not renormalized) as the shift direction.
		const uint normOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Normal);
		const float3 normal = UnpackByte4SNorm(original.Load(normOffset)).xyz;

		const uint colorOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Color);
		const float colorAlpha = (float)(original.Load(colorOffset) >> 24) * (1.0f / 255.0f);

		animated += normal * GetTreeShift(position4.xyz, colorAlpha, data.Params);
	}

	// Skinned meshes: hand the model-space positions to the skinning pass, which writes the final
	// live vertices and previous positions.
	if (skinnedOutput)
	{
		AnimatedPositions[slot][vertexIndex] = float4(animated, sourceW);
		return;
	}

	if (!hasPosition)
		return;

	RWByteAddressBuffer output = OutputVertices[slot];

	// Save the live position as the previous frame's position before overwriting (motion vectors).
	if (fullPrecision)
	{
		PrevPositions[slot][vertexIndex] = asfloat(output.Load3(posOffset));
		output.Store3(posOffset, asuint(animated));
	}
	else
	{
		const uint2 prevPacked = output.Load2(posOffset);
		PrevPositions[slot][vertexIndex] = float3(
			f16tof32(prevPacked.x & 0xFFFF),
			f16tof32(prevPacked.x >> 16),
			f16tof32(prevPacked.y & 0xFFFF));

		const uint packedXy = f32tof16(animated.x) | (f32tof16(animated.y) << 16);
		const uint packedZw = f32tof16(animated.z) | (f32tof16(position4.w) << 16);
		output.Store2(posOffset, uint2(packedXy, packedZw));
	}
}
