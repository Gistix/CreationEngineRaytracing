#pragma once

#include "Core/SkinnedMesh.h"
#include "Framework/DescriptorTableManager.h"

class DynamicMesh : public SkinnedMesh
{
	nvrhi::BufferHandle m_DynamicBuffer;

	DescriptorHandle m_DynamicDescriptor;

	// CPU staging copy used to detect changes (lazy) and feed the GPU upload.
	eastl::vector<uint8_t> m_DynamicData;

	bool m_NeedsUpload = false;
public:
	DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList);

	virtual DynamicMesh* AsDynamicMesh() override { return this; }

	void UpdateDynamicData(void* dynamicData, uint32_t dataSize);

	void UploadBuffers(nvrhi::ICommandList* commandList) override;

	bool IsUpdatable() const override { return true; }
};
