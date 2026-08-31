#include "interop/Mesh.hlsli"
#include "include/WaveSize.hlsli"

cbuffer ArgsConstants : register(b0)
{
    uint NumMeshes;
};

ByteAddressBuffer MeshSlotRemap : register(t0);
StructuredBuffer<Mesh> Meshes : register(t1);

struct IndirectCommand
{
	uint DrawIndex;
	uint VertexCount;
	uint InstanceCount;
	uint StartVertexLocation;
	uint StartInstanceLocation;
};

RWStructuredBuffer<IndirectCommand> IndirectArgs : register(u0);

WAVE_SIZE(32)
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

	IndirectCommand cmd;
	cmd.DrawIndex = i;
	cmd.VertexCount = (uint)mesh.NumTriangles * 3u;
	cmd.InstanceCount = 1u;
	cmd.StartVertexLocation = 0u;
	cmd.StartInstanceLocation = 0u;

	IndirectArgs[i] = cmd;
}