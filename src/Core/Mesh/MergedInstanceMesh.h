#pragma once

#include "Core/Mesh/BaseMesh.h"
#include "Types/RE/GrassInstanceData.h"
#include "Framework/DescriptorTableManager.h"

// One engine grass group baked into a single BLAS: N geometry descs, all referencing the source
// blade index/vertex buffers, each with its own per-instance transform in the mesh manager's grass
// transform pool. The shader resolves the per-instance transform from the hit's GeometryIndex().
// Grass wind: the BLAS reads per-instance swayed blade positions from a float4 buffer in the dynamic
// position table ([current N*V | previous N*V]), written each frame by the VertexAnimation pass.
class MergedInstanceMesh : public BaseMesh
{
public:
	MergedInstanceMesh(
		RE::BSTriShape* a_sourceShape,
		const eastl::vector<RE::GrassInstanceData>& a_instances,
		bool a_uniformScale,
		nvrhi::ICommandList* a_commandList);

	~MergedInstanceMesh() override;

	bool IsMergedInstance() const override { return true; }
	uint32_t GetInstanceTransformBase() const override { return m_InstanceTransformBase; }
	uint32_t GetInstanceCount() const override { return m_InstanceCount; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get()); }
	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }

	void Update(nvrhi::ICommandList* a_commandList) override;

	// Queues the per-frame grass wind evaluation; marks the mesh vertex-dirty when it ran.
	void UpdateWind();

	bool IsUpdatable() const override { return m_WindPositionBuffer != nullptr; }
	uint32_t GetDynamicIndex() const override { return m_WindPositionBuffer ? m_DynamicDescriptor.Get() : 0u; }
	uint16_t GetMeshDataFlags() const override;
	uint32_t GetShadingVertexOffset() const override { return static_cast<uint32_t>(m_VertexBuffer.m_Offset); }
	void CollectOwnedBuffers(OwnedBufferStats& stats) const override;

private:
	bool CreateWindBuffers(RE::BSTriShape* a_sourceShape, uint32_t a_vertexCount, uint16_t a_vertexStride);

	BufferDescriptor m_IndexBuffer;
	BufferDescriptor m_VertexBuffer;

	uint32_t m_InstanceTransformBase = UINT32_MAX;
	uint32_t m_InstanceCount = 0;
	uint32_t m_VertexCount = 0;

	// Per-instance swayed blade positions (float4): current block then previous block.
	nvrhi::BufferHandle m_WindPositionBuffer;
	DescriptorHandle m_DynamicDescriptor;

	// Inverse shape-local transform (recovers vanilla InstanceData1.xy from the pool transforms).
	float3x4 m_InverseShapeLocal = Constants::kIdentityTransform;

	float m_PrevWindTimer = 0.0f;
	bool m_WindActive = false;
	bool m_NeedsWindInit = true;
};
