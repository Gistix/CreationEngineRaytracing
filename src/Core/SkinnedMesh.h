#pragma once

#include "Core/BaseMesh.h"

class SkinnedMesh : public BaseMesh
{
	BufferDescriptor m_VertexBuffer;
	eastl::vector<BufferDescriptor> m_IndexBuffers;
public:
	SkinnedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	virtual SkinnedMesh* AsSkinnedMesh() override { return this; }

	bool IsUpdatable() const override { return true; }

	const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const override { return m_GeometryDescs; }
protected:
	// Non-building constructor for derived meshes that supply their own vertex buffer (e.g. DynamicMesh).
	SkinnedMesh() = default;

	eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescsMutable() override { return m_GeometryDescs; }

	// Builds the per-partition index buffers + geometry descs using the supplied vertex buffer.
	// requireSharedNativeVertexBuffer enforces that every partition references the same native vertex buffer (static skins);
	// dynamic meshes supply their own buffer and pass false.
	void BuildSkinned(RE::BSTriShape* bsTriShape, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, bool requireSharedNativeVertexBuffer);

	// One geometry desc per skin partition (identity transform with the local-to-owner transform baked in).
	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;
};
