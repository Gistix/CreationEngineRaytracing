#pragma once

#include "Core/Mesh/BaseMesh.h"

// Traversal-owned parent for a BSMultiStreamInstanceTriShape carrying grass. It owns no geometry or
// BLAS itself; its per-engine-group MergedInstanceMesh children provide the actual BLASes. Keeping
// it in m_Meshes lets the shape participate in the normal mesh lifecycle (create/update/hide/destroy).
class GrassMesh : public BaseMesh
{
public:
	GrassMesh(RE::BSTriShape* a_sourceShape, nvrhi::ICommandList* a_commandList);

	GrassMesh* AsGrassMesh() override { return this; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return 0; }
	uint16_t GetVertexID() const override { return 0; }

	void Update(nvrhi::ICommandList* a_commandList) override;
};
