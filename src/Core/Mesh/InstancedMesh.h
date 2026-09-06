#pragma once

#include "Core/Mesh/BaseMesh.h"

class InstancedMesh : public BaseMesh
{
public:
	struct InstanceEntry
	{
		float3x4 transform;
		float3x4 prevTransform;
		float alpha;
	};

private:
	BufferDescriptor m_IndexBuffer;
	BufferDescriptor m_VertexBuffer;

	eastl::vector<InstanceEntry> m_InstanceData;

public:
	InstancedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	InstancedMesh* AsInstancedMesh() override { return this; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get()); }
	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }

	uint32_t GetInstanceCount() const { return static_cast<uint32_t>(m_InstanceData.size()); }
	const eastl::vector<InstanceEntry>& GetInstances() const { return m_InstanceData; }

	void Update(nvrhi::ICommandList* commandList) override;
};
