#pragma once

#include "Core/BaseMesh.h"
#include "Constants.h"
#include "Interop/BoneTransform.hlsli"

class SkinnedMesh : public BaseMesh
{
	eastl::vector<BufferDescriptor> m_IndexBuffers;
public:
	SkinnedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	virtual SkinnedMesh* AsSkinnedMesh() override { return this; }

	bool IsUpdatable() const override { return true; }

	const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const override {
		if (m_Flags.none(Flags::DismemberSkinInstance))
			return BaseMesh::GetGeometryDescs();

		return m_VisibleGeometryDescs;
	}

	// Copies raw boneWorld transforms from the game skin instance (no matrix math — that moves to GPU).
	// Returns true if the pose advanced this frame. Must be called while the trishape is alive (traversal).
	void Update(nvrhi::ICommandList* commandList) override;

	uint32_t GetBoneCount() const { return static_cast<uint32_t>(m_BoneWorlds.size()); }

	const eastl::vector<NiTransformPacked>& GetBoneWorlds() const { return m_BoneWorlds; }
	const eastl::vector<NiTransformPacked>& GetSkinToBones() const { return m_SkinToBones; }

	NiTransformPacked GetGeometryWorldInverse() const {
		NiTransformPacked r;
		r.Rot0_Scale = m_GeomInv_Rot0_Scale;
		r.Rot1       = m_GeomInv_Rot1;
		r.Rot2       = m_GeomInv_Rot2;
		r.Translate  = m_GeomInv_Translate;
		return r;
	}

	// Shared bindless slot addressing the original (VertexCopy), live (Vertex/VertexWrite) and
	// prev-position (PrevPosition/PrevPositionWrite) buffers; consumed by the skinning pass.
	uint32_t GetSkinningSlot() const { return m_VertexBuffer.m_Descriptor.Get(); }

	uint32_t GetVertexCount() const { return m_VertexCount; }

	bool GetModelSpaceNormal() const { return m_ModelSpaceNormal; }

	uint16_t GetIndexID(size_t geometryIndex) const override { 
		if (m_Flags.none(Flags::DismemberSkinInstance))
			return static_cast<uint16_t>(m_IndexBuffers[geometryIndex].m_Descriptor.Get());

		if (geometryIndex >= m_VisibleGeometrySourceIndices.size())
			return 0;

		return static_cast<uint16_t>(m_IndexBuffers[m_VisibleGeometrySourceIndices[geometryIndex]].m_Descriptor.Get());
	}

	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get()); }
protected:
	// Non-building constructor for derived meshes that supply their own vertex buffer (e.g. DynamicMesh).
	SkinnedMesh() = default;

	// Builds the per-partition index buffers + geometry descs using the supplied vertex buffer.
	// requireSharedNativeVertexBuffer enforces that every partition references the same native vertex buffer (static skins);
	// dynamic meshes supply their own buffer and pass false.
	void BuildSkinned(RE::BSTriShape* bsTriShape, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, bool requireSharedNativeVertexBuffer);

	// Populates m_SkinToBones from the static skin data, called once during construction.
	void InitSkinToBones(RE::NiSkinInstance* skinInstance);

	// Initialize dismember skin instance
	void InitDismemberSkin(RE::NiSkinInstance* skinInstance);

	// Creates the live (skinning output) byte-address UAV buffer seeded from the CPU rest-pose data, plus
	// the prev-position buffer, and registers original/live/prev-position at the shared slot. Repoints the RT
	// read (VertexDescriptors) to the live buffer. Returns the live buffer for the BLAS geometry desc.
	void CreateSkinningBuffers(nvrhi::ICommandList* commandList, RE::BSGraphics::TriShape* sourceTriShape, uint32_t vertexCount, uint16_t vertexStride);

	void RefreshVisibleGeometryCache();

	// One entry per skin partition.
	eastl::vector<bool> m_PartitionVisibility;

	mutable eastl::vector<nvrhi::rt::GeometryDesc> m_VisibleGeometryDescs;
	mutable eastl::vector<size_t> m_VisibleGeometrySourceIndices;

	// Native (rest-pose) byte-address vertex buffer; the original source consumed by the skinning pass.
	BufferDescriptor m_VertexBuffer;

	// Maps each emitted geometry desc back to its source skin partition index.
	eastl::vector<size_t> m_GeometryPartitionIndices;

	// Live (skinning output) byte-address vertices read by the RT path; copy of the native original buffer.
	nvrhi::BufferHandle m_LiveVertexBuffer;

	// Previous skinned positions for per-vertex motion vectors.
	nvrhi::BufferHandle m_PrevPositionBuffer;

	uint32_t m_VertexCount = 0;

	// Raw boneWorld transforms (one per bone), copied from the engine each frame the pose advances.
	eastl::vector<NiTransformPacked> m_BoneWorlds;

	// Static skinToBone transforms (one per bone), populated once at creation from skinData.
	eastl::vector<NiTransformPacked> m_SkinToBones;

	// Per-frame geometry-world-inverse (flattened to avoid alignas(16) on the class).
	float4 m_GeomInv_Rot0_Scale;
	float4 m_GeomInv_Rot1;
	float4 m_GeomInv_Rot2;
	float4 m_GeomInv_Translate;

	// Skin instance frame id of the last pose we processed (skip work when the animation hasn't advanced).
	uint32_t m_SkinFrameID = Constants::INVALID_FRAME_ID;

	bool m_ModelSpaceNormal = false;
};
