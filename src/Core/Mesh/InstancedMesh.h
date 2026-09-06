#pragma once

#include "Core/Mesh/BaseMesh.h"

class InstancedMesh : public BaseMesh
{
	BufferDescriptor m_IndexBuffer;
	BufferDescriptor m_VertexBuffer;

	struct InstanceData
	{
		RE::NiTransform transorm;
		float alpha;
	};

	eastl::vector<InstanceData> m_InstanceData;
public:
	InstancedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	InstancedMesh* AsInstancedMesh() override { return this; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get()); }
	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }
};
