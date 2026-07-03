#pragma once

#include "Core/SkinnedMesh.h"

class DismemberMesh : public SkinnedMesh
{
public:
	DismemberMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList);

	virtual SkinnedMesh* AsSkinnedMesh() override { return this; }

	virtual DismemberMesh* AsDismemberMesh() override { return this; }

	const eastl::vector<nvrhi::rt::GeometryDesc>& GetGeometryDescs() const override;

	uint16_t GetIndexID(size_t geometryIndex) const override;

	void Update(nvrhi::ICommandList* commandList) override;

	const eastl::vector<bool>& GetPartitionVisibility() const { return m_PartitionVisibility; }

	bool IsPartitionVisible(size_t index) const;

private:
	void RefreshVisibleGeometryCache() const;

	// One entry per skin partition.
	eastl::vector<bool> m_PartitionVisibility;

	mutable eastl::vector<nvrhi::rt::GeometryDesc> m_VisibleGeometryDescs;
	mutable eastl::vector<size_t> m_VisibleGeometrySourceIndices;
};
