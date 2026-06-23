#pragma once

#include "Core/BaseMesh.h"

class DirectMesh : public BaseMesh
{
	BufferDescriptor m_IndexBuffer;
	BufferDescriptor m_VertexBuffer;

	// Single geometry desc for the trishape (identity transform with the local-to-owner transform baked in).
	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;
public:
	DirectMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const override { return m_GeometryDescs; }
protected:
	eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescsMutable() override { return m_GeometryDescs; }
};
