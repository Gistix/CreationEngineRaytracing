#pragma once

#include "Core/SkinnedMesh.h"

class DismemberMesh : public SkinnedMesh
{
public:
	DismemberMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	DismemberMesh* AsDismemberMesh() override { return this; }

	bool Update() override;

	const eastl::vector<uint8_t>& GetPartitionVisibility() const { return m_PartitionVisibility; }

	bool IsPartitionVisible(size_t index) const;

private:
	// One entry per skin partition; 1 means editor-visible, 0 means hidden.
	eastl::vector<uint8_t> m_PartitionVisibility;
};
