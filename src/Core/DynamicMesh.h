#pragma once

#include "Core/SkinnedMesh.h"

class DynamicMesh : public SkinnedMesh
{
	RE::BSDynamicTriShape* m_BSDynamicTriShape;
	nvrhi::BufferHandle m_DynamicBuffer;
public:
	DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList);

	void Update(nvrhi::ICommandList* commandList) override;
};
