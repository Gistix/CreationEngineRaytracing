#pragma once

#include "Core/BaseMesh.h"

class SceneGraph;
class SubIndexSegmentMesh;

// Manager for a BSSubIndexTriShape. Owns the shared GPU resources (index/vertex buffer
// handles, vertex stride, vertex desc) and a vector of SubIndexSegmentMesh children,
// one per currently-known segment. Each child is a regular BaseMesh and lives in its
// own BLASCluster (in SceneGraph::m_SubIndexSegmentClusters) so it gets its own BLAS,
// InstanceData, and TLAS entry.
//
// SubIndexMesh itself is a BaseMesh in SceneGraph::m_DirectMeshes (for lifecycle
// tracking via the existing OnDestroy / deferred-destroy flow) but is NOT a member
// of any BLASCluster; its m_GeometryDescs is empty.
//
// Segment identity: segments are keyed by (start, numTris) in m_SegmentMap — the
// segment's position in the parent's index buffer. This is stable across frames
// even if the engine reorders the segment data array. Segments whose engine-side
// visibility flag goes to 0 are hidden (State::SubIndexHidden) rather than destroyed;
// the SubIndexMesh owns them for the parent's lifetime and reuses them when the
// engine flips the flag back on.
class SubIndexMesh : public BaseMesh
{
	// Shared across all K SubIndexSegmentMesh children. Created once in ctor from
	// the parent's D3D12 resources; released when the manager is destroyed.
	nvrhi::BufferHandle m_IndexBuffer;
	nvrhi::BufferHandle m_VertexBuffer;

	DescriptorHandle m_IndexDescriptor;
	DescriptorHandle m_VertexDescriptor;

	RE::BSGraphics::VertexDesc m_VertexDesc;

	eastl::vector<eastl::unique_ptr<SubIndexSegmentMesh>> m_Segments;

	// Key: (start << 32) | numTris — the segment's identity in the parent's index buffer.
	// Non-owning lookup into m_Segments.
	eastl::hash_map<uint64_t, SubIndexSegmentMesh*> m_SegmentMap;

	void CreateSegment(uint32_t start, uint32_t numTris);

public:
	SubIndexMesh(RE::BSSubIndexTriShape* triShape);

	virtual void SetHidden(bool hidden) override;

	// Stable segment identity: (segment.index, segment.numTris) packed into a 64-bit key.
	// The engine may reorder the segmentData array between frames, so iteration order
	// is not a stable key — but (start, numTris) is, because it identifies the
	// segment's position in the parent's index buffer.
	inline uint64_t MakeSegmentKey(uint32_t start, uint32_t numTris)
	{
		return (static_cast<uint64_t>(start) << 32) | static_cast<uint64_t>(numTris);
	}

	nvrhi::BufferHandle GetIndexBuffer() const { return m_IndexBuffer; }
	nvrhi::BufferHandle GetVertexBuffer() const { return m_VertexBuffer; }
	const RE::BSGraphics::VertexDesc& GetVertexDesc() const { return m_VertexDesc; }

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override
	{
		return static_cast<uint16_t>(m_IndexDescriptor.Get());
	}

	uint16_t GetVertexID() const override
	{
		return static_cast<uint16_t>(m_VertexDescriptor.Get());
	}

	// Sync the K SubIndexSegmentMesh children with the parent's current segment
	// visibility. Segments are identified by (start, numTris); new segments are
	// created, existing segments have their SubIndexHidden flag toggled to match
	// the engine's segment flag. Segments missing from runtimeData (engine removed
	// them) are marked SubIndexHidden.
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
