#pragma once

#include "Core/BaseMesh.h"

class DirectMesh : public BaseMesh
{
	BufferDescriptor m_IndexBuffer;
protected:
	BufferDescriptor m_VertexBuffer;
public:
	DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get()); }

	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }
};
