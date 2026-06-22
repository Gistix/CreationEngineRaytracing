#pragma once

#include "Core/SkinnedMesh.h"

class DynamicMesh : public SkinnedMesh
{
	RE::BSDynamicTriShape* m_BSDynamicTriShape;
	nvrhi::BufferHandle m_DynamicBuffer;

	// CPU staging copy used to detect changes (lazy) and feed the GPU upload.
	eastl::vector<uint8_t> m_DynamicData;
	bool m_NeedsUpload = false;
public:
	DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList);

	bool UpdateData() override;

	void UploadBuffers(nvrhi::ICommandList* commandList) override;

	bool IsUpdatable() const override { return true; }
};
