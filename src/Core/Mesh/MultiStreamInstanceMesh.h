#pragma once

#include "Core/Mesh/BaseMesh.h"

// Traversal-owned parent for a BSMultiStreamInstanceTriShape (e.g. grass). It owns no geometry or
// BLAS itself; its per-group MergedInstanceMesh children provide the actual BLASes. Keeping
// it in m_Meshes lets the shape participate in the normal mesh lifecycle (create/update/hide/destroy).
class MultiStreamInstanceMesh : public BaseMesh
{
public:
	MultiStreamInstanceMesh(RE::BSTriShape* a_sourceShape, nvrhi::ICommandList* a_commandList);

	MultiStreamInstanceMesh* AsMultiStreamInstanceMesh() override { return this; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return 0; }
	uint16_t GetVertexID() const override { return 0; }

	void Update(nvrhi::ICommandList* a_commandList) override;
};

using GrassMesh = MultiStreamInstanceMesh;
