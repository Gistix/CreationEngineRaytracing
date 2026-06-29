#pragma once

#include "Core/BaseMesh.h"

class DirectMesh : public BaseMesh
{
	BufferDescriptor m_IndexBuffer;
protected:
	BufferDescriptor m_VertexBuffer;

	// Single geometry desc for the trishape (identity transform with the local-to-owner transform baked in).
	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;
public:
	DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const override { return m_GeometryDescs; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get()); }

	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }
protected:
	eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescsMutable() override { return m_GeometryDescs; }
};
