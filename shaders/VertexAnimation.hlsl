#include "Interop/VertexAnimation.hlsli"
#include "Interop/Transform.hlsli"
#include "Interop/VertexDesc.hlsli"
#include "include/WaveSize.hlsli"
#include "include/Vulkan.hlsli"

StructuredBuffer<VertexAnimationData> AnimationData : register(t0);
StructuredBuffer<GrassWindData> GrassData           : register(t1);
// Per-instance blade transforms of merged grass groups (shape-local * instance).
StructuredBuffer<Transform> InstanceTransforms      : register(t2);

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
// Dynamic float4 positions read by the BLAS and shading; grass groups store [current | previous] here. Dynamic slot.
VK_BINDING(6, 0) RWStructuredBuffer<float4> DynamicPositionsOut[]     : register(u0, space2);

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

// Matches CS Common/GrassInstance.hlsli WindScalar (vanilla grass sway per instance).
float GrassWindScalar(float2 instanceXY, float timer)
{
	const float phase = 0.4f * ((instanceXY.x + instanceXY.y) * -0.0078125f + timer);
	float sine, cosine;
	sincos(phase, sine, cosine);
	const float PI = 3.14159265f;
	const float cosineTerm = 0.2f * cos(PI * cosine);
	const float sineTerm = sin(PI * sine);
	const float harmonicTerm = sin(2.0f * PI * sine);
	return (sineTerm + harmonicTerm) * 0.3f + cosineTerm;
}

float3x3 Inverse3x3(float3x3 m)
{
	const float3 c0 = cross(m[1], m[2]);
	const float3 c1 = cross(m[2], m[0]);
	const float3 c2 = cross(m[0], m[1]);
	const float det = dot(m[0], c0);
	return transpose(float3x3(c0, c1, c2)) * (1.0f / det);
}

// One thread per (instance, blade vertex) of a merged grass group: vanilla RunGrass wind, evaluated for the
// current and previous wind timers, written in blade space.
WAVE_SIZE(32)
[numthreads(64, 1, 1)]
void GrassMain(uint2 DTid : SV_DispatchThreadID)
{
	const GrassWindData data = GrassData[DTid.y];

	const uint total = data.InstanceCount * data.VertexCount;
	if (DTid.x >= total)
		return;

	const uint instanceIndex = DTid.x / data.VertexCount;
	const uint vertexIndex = DTid.x - instanceIndex * data.VertexCount;

	VertexDesc vertexDesc = data.VertexDesc;

	const uint vertexOffset = (uint)vertexDesc.GetVertexSize() * vertexIndex;
	const uint posOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Position);

	ByteAddressBuffer original = OriginalVertices[NonUniformResourceIndex(data.Index)];

	float3 position;
	if (vertexDesc.HasFlag(VertexFlags::FullPrec))
	{
		position = asfloat(original.Load3(posOffset));
	}
	else
	{
		const uint2 packed = original.Load2(posOffset);
		position = float3(f16tof32(packed.x & 0xFFFF), f16tof32(packed.x >> 16), f16tof32(packed.y & 0xFFFF));
	}

	// Vanilla weights the sway by the squared vertex color alpha.
	float colorAlpha = 1.0f;
	if (vertexDesc.HasFlag(VertexFlags::Colors))
	{
		const uint colorOffset = vertexOffset + vertexDesc.GetAttributeOffset(VertexAttribute::Color);
		colorAlpha = (float)(original.Load(colorOffset) >> 24) * (1.0f / 255.0f);
	}

	const Transform instanceTransform = InstanceTransforms[data.InstanceTransformBase + instanceIndex];
	const float3x4 xf = instanceTransform.Transform;

	// Instance origin before the shape-local transform (vanilla InstanceData1.xy).
	const float3 poolTranslation = float3(xf._m03, xf._m13, xf._m23);
	const float2 instanceXY = mul(data.InverseShapeLocal, float4(poolTranslation, 1.0f)).xy;

	// Bring the wind direction back into blade space so the pool transform reproduces the vanilla offset.
	const float3 windBlade = mul(Inverse3x3((float3x3)xf), data.Wind.xyz);
	const float weight = data.Wind.w * 0.5f * colorAlpha * colorAlpha;

	const float current = weight * GrassWindScalar(instanceXY, data.Timers.x);
	const float previous = weight * GrassWindScalar(instanceXY, data.Timers.y);

	RWStructuredBuffer<float4> output = DynamicPositionsOut[NonUniformResourceIndex(data.DynamicIndex)];
	output[DTid.x] = float4(position + windBlade * current, 1.0f);
	output[total + DTid.x] = float4(position + windBlade * previous, 1.0f);
}
