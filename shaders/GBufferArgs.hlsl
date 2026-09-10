#include "interop/Mesh.hlsli"
#include "include/WaveSize.hlsli"
#include "include/Vulkan.hlsli"

struct ArgsConstants
{
    uint NumMeshes;
};
VK_PUSH_CONSTANT ConstantBuffer<ArgsConstants> PC : register(b0);

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

	if (i >= PC.NumMeshes)
		return;

	const uint packed = MeshSlotRemap.Load(i * 4);
	const uint geometrySlot = packed & 0xFFFF;
	const uint instanceIndex = packed >> 16;

	const Mesh mesh = Meshes[NonUniformResourceIndex(geometrySlot)];

	IndirectCommand cmd;
	cmd.DrawIndex = i;
	cmd.VertexCount = (uint)mesh.NumTriangles * 3u;
	cmd.InstanceCount = (mesh.Type == MeshType::Grass) ? max(mesh.GrassInstanceCount, 1u) : 1u;
	cmd.StartVertexLocation = 0u;
	cmd.StartInstanceLocation = 0u;

	IndirectArgs[i] = cmd;
}