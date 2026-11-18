#pragma once

#include "Core/Mesh/SkinnedMesh.h"
#include "Framework/DescriptorTableManager.h"

class DynamicMesh : public SkinnedMesh
{
	// Live (skinning output) float4 positions; read by the BLAS/RT path.
	nvrhi::BufferHandle m_DynamicBuffer;

	// Original (morph) float4 positions copied from the game; VertexAnimation source, which writes the
	// pre-skin animated positions the skinning pass consumes.
	nvrhi::BufferHandle m_OriginalDynamicBuffer;

	// Dynamic bindless slot: original in DynamicVertexReadDescriptors (SRV), live in DynamicVertexWriteDescriptors (UAV).
	DescriptorHandle m_DynamicDescriptor;

	// CPU staging copy used to detect changes (lazy) and feed the GPU upload.
	eastl::vector<uint8_t> m_DynamicData;

	bool m_NeedsUpload = false;
public:
	DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList);

	virtual DynamicMesh* AsDynamicMesh() override { return this; }

	virtual SkinnedMesh* AsSkinnedMesh() override { return nullptr; }

	// Shared bindless slot for the dynamic float4 buffers (original SRV + live UAV).
	uint32_t GetDynamicIndex() const override { return m_DynamicDescriptor.Get(); }

	// Called by hook
	void UpdateDynamicData(void* dynamicData, uint32_t dataSize);

	void SetSkinningBufferStates(nvrhi::ICommandList* commandList, bool writing) override;
	void CollectOwnedBuffers(OwnedBufferStats& stats) const override;

	bool IsUpdatable() const override { return true; }

protected:
	// Uploads the latest morph positions when the game changed them.
	bool UpdateAnimationSource(nvrhi::ICommandList* commandList) override;
};
