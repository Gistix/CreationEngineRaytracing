#pragma once

#include "Core/Mesh/Mesh.h"
#include "Types/TreeAnim.h"

// Static (non-skinned) geometry animated by vanilla TREE_ANIM (leaf sway along the vertex normal,
// weighted by vertex color alpha). Owns a live vertex buffer the VertexAnimation pass rewrites from
// the rest pose, plus previous positions for motion vectors. Skinned trees use SkinnedMesh instead.
class TreeAnimMesh : public Mesh
{
	nvrhi::BufferHandle m_LiveVertexBuffer;
	nvrhi::BufferHandle m_PrevPositionBuffer;

	uint32_t m_VertexCount = 0;

	TreeAnimState m_TreeAnim;

public:
	TreeAnimMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	bool IsUpdatable() const override { return true; }

	void Update(nvrhi::ICommandList* commandList) override;
	void CollectOwnedBuffers(OwnedBufferStats& stats) const override;
};
