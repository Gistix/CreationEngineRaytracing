#pragma once

#include "Core/Mesh/BaseMesh.h"
#include "Types/RE/GrassInstanceData.h"

// One engine grass group baked into a single BLAS: N geometry descs, all referencing the source
// blade index/vertex buffers, each with its own per-instance transform in the mesh manager's grass
// transform pool. The shader resolves the per-instance transform from the hit's GeometryIndex().
class MergedInstanceMesh : public BaseMesh
{
public:
	MergedInstanceMesh(
		RE::BSTriShape* a_sourceShape,
		const eastl::vector<RE::GrassInstanceData>& a_instances,
		bool a_uniformScale,
		nvrhi::ICommandList* a_commandList);

	~MergedInstanceMesh() override;

	bool IsGroupedInstance() const override { return true; }
	uint32_t GetGrassTransformBase() const override { return m_GrassTransformBase; }
	uint32_t GetGrassInstanceCount() const override { return m_GrassInstanceCount; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get()); }
	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }

	void Update(nvrhi::ICommandList* a_commandList) override;

private:
	BufferDescriptor m_IndexBuffer;
	BufferDescriptor m_VertexBuffer;

	uint32_t m_GrassTransformBase = UINT32_MAX;
	uint32_t m_GrassInstanceCount = 0;
};
