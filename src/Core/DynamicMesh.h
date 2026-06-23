#pragma once

#include "Core/SkinnedMesh.h"
#include "Framework/DescriptorTableManager.h"

class DynamicMesh : public SkinnedMesh
{
	// Live (skinning output) float4 positions; read by the BLAS/RT path.
	nvrhi::BufferHandle m_DynamicBuffer;

	// Original (rest/morph) float4 positions copied from the game each frame; skinning input.
	nvrhi::BufferHandle m_OriginalDynamicBuffer;

	// Shared bindless slot: original in DynamicVertexDescriptors (SRV), live in DynamicVertexWriteDescriptors (UAV).
	DescriptorHandle m_DynamicDescriptor;

	// CPU staging copy used to detect changes (lazy) and feed the GPU upload.
	eastl::vector<uint8_t> m_DynamicData;

	bool m_NeedsUpload = false;
public:
	DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList);

	virtual DynamicMesh* AsDynamicMesh() override { return this; }

	// Shared bindless slot for the dynamic float4 buffers (original SRV + live UAV).
	uint32_t GetDynamicIndex() const { return m_DynamicDescriptor.Get(); }

	void UpdateDynamicData(void* dynamicData, uint32_t dataSize);

	void UploadBuffers(nvrhi::ICommandList* commandList) override;

	bool IsUpdatable() const override { return true; }

protected:
	// Dynamic positions live in the dynamic buffer (deferred: positions-only for now).
	uint16_t GetVertexID() const override { return static_cast<uint16_t>(m_DynamicDescriptor.Get()); }
};
