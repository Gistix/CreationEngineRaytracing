#pragma once

#include "Core/BaseMesh.h"
#include "Constants.h"

class SkinnedMesh : public BaseMesh
{
	eastl::vector<BufferDescriptor> m_IndexBuffers;
public:
	SkinnedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	virtual SkinnedMesh* AsSkinnedMesh() override { return this; }

	bool IsUpdatable() const override { return true; }

	// Recomputes the per-bone skinning matrices (geometry-local) from the game skin instance.
	// Returns true if the pose advanced this frame. Must be called while the trishape is alive (traversal).
	bool Update() override;

	const eastl::vector<float3x4>& GetBoneMatrices() const { return m_BoneMatrices; }

	// Shared bindless slot addressing the original (VertexCopy), live (Vertex/VertexWrite) and
	// prev-position (PrevPosition/PrevPositionWrite) buffers; consumed by the skinning pass.
	uint32_t GetSkinningSlot() const { return m_VertexBuffer.m_Descriptor.Get(); }

	uint32_t GetVertexCount() const { return m_VertexCount; }

	const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const override { return m_GeometryDescs; }

	bool GetModelSpaceNormal() const { return m_ModelSpaceNormal; }

	uint16_t GetIndexID(size_t geometryIndex) const override { return static_cast<uint16_t>(m_IndexBuffers[geometryIndex].m_Descriptor.Get()); }

	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }
protected:
	// Non-building constructor for derived meshes that supply their own vertex buffer (e.g. DynamicMesh).
	SkinnedMesh() = default;

	eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescsMutable() override { return m_GeometryDescs; }

	// Builds the per-partition index buffers + geometry descs using the supplied vertex buffer.
	// requireSharedNativeVertexBuffer enforces that every partition references the same native vertex buffer (static skins);
	// dynamic meshes supply their own buffer and pass false.
	void BuildSkinned(RE::BSTriShape* bsTriShape, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, bool requireSharedNativeVertexBuffer);

	// Creates the live (skinning output) byte-address UAV buffer seeded from the CPU rest-pose data, plus
	// the prev-position buffer, and registers original/live/prev-position at the shared slot. Repoints the RT
	// read (VertexDescriptors) to the live buffer. Returns the live buffer for the BLAS geometry desc.
	void CreateSkinningBuffers(nvrhi::ICommandList* commandList, RE::BSGraphics::TriShape* sourceTriShape, uint32_t vertexCount, uint16_t vertexStride);

	// Native (rest-pose) byte-address vertex buffer; the original source consumed by the skinning pass.
	BufferDescriptor m_VertexBuffer;

	// One geometry desc per skin partition (identity transform with the local-to-owner transform baked in).
	eastl::vector<nvrhi::rt::GeometryDesc> m_GeometryDescs;

	// Live (skinning output) byte-address vertices read by the RT path; copy of the native original buffer.
	nvrhi::BufferHandle m_LiveVertexBuffer;

	// Previous skinned positions for per-vertex motion vectors.
	nvrhi::BufferHandle m_PrevPositionBuffer;

	uint32_t m_VertexCount = 0;

	// Geometry-local bone matrices, recomputed each frame the pose advances; consumed by the skinning pass.
	eastl::vector<float3x4> m_BoneMatrices;

	// Skin instance frame id of the last pose we processed (skip work when the animation hasn't advanced).
	uint32_t m_SkinFrameID = Constants::INVALID_FRAME_ID;

	bool m_ModelSpaceNormal = false;
};
