#include "interop/Mesh.hlsli"
#include "interop/Material/MaterialBaseData.hlsli"
#include "include/WaveSize.hlsli"
#include "include/Vulkan.hlsli"

struct ArgsConstants
{
    uint NumMeshes;
};
VK_PUSH_CONSTANT ConstantBuffer<ArgsConstants> PC : register(b0);

ByteAddressBuffer MeshSlotRemap : register(t0);
StructuredBuffer<Mesh> Meshes : register(t1);
ByteAddressBuffer MaterialBuffer : register(t2);

struct IndirectCommand
{
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

	// Water-only: cull every non-water mesh by emitting an empty draw.
	// Procedural/displacement water never reaches this buffer (culled in
	// SceneGraph), so the exclusion is preserved automatically.
	// The mesh slot index travels in StartInstanceLocation so the vertex
	// shader can recover it via SV_InstanceID on every backend (no indirect
	// push constants, which Vulkan does not support).
	const uint typeFeature = MaterialBuffer.Load(mesh.GetMaterialOffset());
	const uint16_t matType = (uint16_t)(typeFeature & 0xFFFFu);
	const bool isWater = (matType == Type::Water);

	IndirectCommand cmd;
	cmd.VertexCount = isWater ? (uint)mesh.NumTriangles * 3u : 0u;
	cmd.InstanceCount = isWater ? 1u : 0u;
	cmd.StartVertexLocation = 0u;
	cmd.StartInstanceLocation = i;

	IndirectArgs[i] = cmd;
}
