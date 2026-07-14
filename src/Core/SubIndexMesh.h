#pragma once

#include "Core/BaseMesh.h"

class SceneGraph;
class SubIndexSegmentMesh;

// Manager for a BSSubIndexTriShape. Owns the shared GPU resources (index/vertex buffer
// handles, vertex stride, vertex desc) and a vector of SubIndexSegmentMesh children,
// one per currently-visible segment. Each child is a regular BaseMesh and lives in its
// own BLASCluster (in SceneGraph::m_SubIndexSegmentClusters) so it gets its own BLAS,
// InstanceData, and TLAS entry.
//
// SubIndexMesh itself is a BaseMesh in SceneGraph::m_DirectMeshes (for lifecycle
// tracking via the existing OnDestroy / deferred-destroy flow) but is NOT a member
// of any BLASCluster; its m_GeometryDescs is empty.
class SubIndexMesh : public BaseMesh
{
	SceneGraph* m_SceneGraph = nullptr;

	// Shared across all K SubIndexSegmentMesh children. Created once in ctor from
	// the parent's D3D12 resources; released when the manager is destroyed.
	nvrhi::BufferHandle m_SharedIndexBuffer;
	nvrhi::BufferHandle m_SharedVertexBuffer;

	RE::BSGraphics::VertexDesc m_VertexDesc;

	eastl::vector<eastl::unique_ptr<SubIndexSegmentMesh>> m_Segments;
	eastl::hash_map<uint32_t, SubIndexSegmentMesh*> m_SegmentMap;  // non-owning lookup

	void CreateSegment(uint32_t segmentIndex);
	void DestroySegment(uint32_t segmentIndex);

public:
	SubIndexMesh(RE::BSSubIndexTriShape* triShape, SceneGraph* sceneGraph);

	SceneGraph* GetSceneGraph() const { return m_SceneGraph; }
	nvrhi::BufferHandle GetSharedIndexBuffer() const { return m_SharedIndexBuffer; }
	nvrhi::BufferHandle GetSharedVertexBuffer() const { return m_SharedVertexBuffer; }
	const RE::BSGraphics::VertexDesc& GetVertexDesc() const { return m_VertexDesc; }

	// SubIndexMesh has no geometry of its own (m_GeometryDescs is empty). These are
	// only required to make the class non-abstract — they're never called because
	// SubIndexMesh is not a member of any BLASCluster.
	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override { return 0; }
	uint16_t GetVertexID() const override { return 0; }

	// Sync the K SubIndexSegmentMesh children with the parent's current segment
	// visibility. Creates new segments for newly-visible flags, destroys segments
	// whose flags went to zero. Called from the traversal update path.
	void Update(nvrhi::ICommandList* commandList) override;

	// Destroy all K SubIndexSegmentMesh children. Used when the manager is hidden
	// (parent not visited this frame) and when OnDestroy is called.
	void DestroyAllSegments();

	// Called by SceneGraph::OnDestroy when the BSSubIndexTriShape is being destroyed.
	// Nulls m_BSTriShape and destroys all K segments (removing each from its cluster
	// and from SceneGraph::m_SubIndexSegmentClusters). The SubIndexMesh itself is then
	// deferred-destroyed by the existing pending-destroy flow.
	void OnDestroy() override;

	SubIndexMesh* AsSubIndexMesh() override { return this; }
};
