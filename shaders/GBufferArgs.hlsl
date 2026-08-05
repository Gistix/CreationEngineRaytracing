#include "interop/Mesh.hlsli"

cbuffer ArgsConstants : register(b0)
{
    uint NumMeshes;
};

ByteAddressBuffer MeshSlotRemap : register(t0);
StructuredBuffer<Mesh> Meshes : register(t1);

RWStructuredBuffer<uint4> IndirectArgs : register(u0);

[numthreads(64, 1, 1)]
void Main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	const uint i = dispatchThreadID.x;

	if (i >= NumMeshes)
		return;

	const uint packed = MeshSlotRemap.Load(i * 4);
	const uint geometrySlot = packed & 0xFFFF;
	const uint instanceIndex = packed >> 16;

	const Mesh mesh = Meshes[NonUniformResourceIndex(geometrySlot)];

	// DrawIndirectArguments { vertexCount, instanceCount, startVertexLocation, startInstanceLocation }
	IndirectArgs[i] = uint4((uint)mesh.NumTriangles * 3u, 1u, 0u, i);
}