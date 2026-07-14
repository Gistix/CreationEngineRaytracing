#pragma once

#include "Core/BaseMesh.h"

class SubIndexMesh;

// One segment of a BSSubIndexTriShape, exposed as a regular BaseMesh so it can be
// a first-class cluster member with its own BLAS / InstanceData / TLAS entry.
// The actual GPU resources (index/vertex buffer handles, vertex stride, vertex desc)
// live in the SubIndexMesh manager; this class only owns its own two descriptor slots
// in the bindless tables.
//
// m_Manager is a non-owning raw pointer: the SubIndexMesh owns the SubIndexSegmentMesh
// via unique_ptr in its m_Segments vector, so the segment is destroyed before the manager.
class SubIndexSegmentMesh : public BaseMesh
{
	SubIndexMesh* m_Manager = nullptr;

	DescriptorHandle m_IndexDescriptor;
	DescriptorHandle m_VertexDescriptor;

public:
	SubIndexSegmentMesh(
		SubIndexMesh* manager,
		RE::BSSubIndexTriShape* parent,
		uint32_t segmentIndex,
		nvrhi::ICommandList* commandList);

	uint16_t GetIndexID([[maybe_unused]] size_t geometryIndex) const override
	{
		return static_cast<uint16_t>(m_IndexDescriptor.Get());
	}

	uint16_t GetVertexID() const override
	{
		return static_cast<uint16_t>(m_VertexDescriptor.Get());
	}

	// Removes this segment from its cluster (if any) so the cluster no longer
	// references it. Safe to call from any thread; the cluster guards its member
	// list with a mutex. Does not destroy the segment — the owning SubIndexMesh
	// manager owns its lifetime.
	void DetachFromCluster();

	void Update(nvrhi::ICommandList* commandList) override;
};
