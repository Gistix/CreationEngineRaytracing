#pragma once

#include "Core/BaseMesh.h"

class SkinnedMesh : public BaseMesh
{
	nvrhi::BufferHandle m_VertexBuffer;
	eastl::vector<nvrhi::BufferHandle> m_IndexBuffers;
	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;
public:
	SkinnedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	void Update(nvrhi::ICommandList* commandList) override;

protected:
	// Non-building constructor for derived meshes that supply their own vertex buffer (e.g. DynamicMesh).
	SkinnedMesh() = default;

	// Builds the per-partition index buffers + geometry descs using the supplied vertex buffer, then builds the BLAS.
	// requireSharedNativeVertexBuffer enforces that every partition references the same native vertex buffer (static skins);
	// dynamic meshes supply their own buffer and pass false.
	void BuildSkinned(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, bool requireSharedNativeVertexBuffer);
};
