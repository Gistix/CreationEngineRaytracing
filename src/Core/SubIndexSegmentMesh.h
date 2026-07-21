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
//
// Identity: a segment is uniquely identified by (m_Start, m_NumTris) — the index
// range in the parent's index buffer. This is stable across frames even if the engine
// reorders the segment data array.
class SubIndexSegmentMesh : public BaseMesh
{
	SubIndexMesh* m_Manager = nullptr;

	// Identity: (segment.index, segment.numTris) from the parent's segment data.
	uint32_t m_Start = 0;
	uint32_t m_NumTris = 0;

public:
	SubIndexSegmentMesh(SubIndexMesh* manager, RE::BSSubIndexTriShape* parent, uint32_t start, uint32_t numTris);

	virtual uint16_t GetIndexID(size_t geometryIndex) const override;
	virtual uint16_t GetVertexID() const override;
	virtual void SetSubIndexHidden(bool subIndexHidden);

	// Copies world state (transform, worldBound, properties, material) from the
	// manager, clears dirty flags, and marks Transform dirty if it changed.
	void SyncFrom(const SubIndexMesh* manager);

	uint32_t GetStart() const { return m_Start; }
	uint32_t GetNumTris() const { return m_NumTris; }
};
