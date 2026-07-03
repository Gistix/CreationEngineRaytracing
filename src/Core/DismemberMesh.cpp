#include "Core/DismemberMesh.h"

#include "Util.h"

DismemberMesh::DismemberMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList) :
	SkinnedMesh(bsTriShape, commandList)
{
	if (!bsTriShape)
		return;

	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();
	Util::Geometry::GetDismemberPartitionVisibility(geometryData.skinInstance.get(), m_PartitionVisibility);

	logger::info("Partitions: {} - {}", m_PartitionVisibility.size(), fmt::ptr(geometryData.skinInstance.get()));
	for (size_t i = 0; i < m_PartitionVisibility.size(); i++)
		logger::info("\tVisible[{}]: {}", i, m_PartitionVisibility[i]);
}

void DismemberMesh::RefreshVisibleGeometryCache() const
{
	m_VisibleGeometryDescs.clear();
	m_VisibleGeometrySourceIndices.clear();

	m_VisibleGeometryDescs.reserve(m_GeometryDescs.size());
	m_VisibleGeometrySourceIndices.reserve(m_GeometryDescs.size());

	for (size_t i = 0; i < m_GeometryDescs.size(); ++i) {
		const auto partitionIndex = (i < m_GeometryPartitionIndices.size()) ? m_GeometryPartitionIndices[i] : i;
		if (!IsPartitionVisible(partitionIndex))
			continue;

		m_VisibleGeometryDescs.push_back(m_GeometryDescs[i]);
		m_VisibleGeometrySourceIndices.push_back(i);
	}
}

const eastl::vector<nvrhi::rt::GeometryDesc>& DismemberMesh::GetGeometryDescs() const
{
	return m_VisibleGeometryDescs;
}

uint16_t DismemberMesh::GetIndexID(size_t geometryIndex) const
{
	if (geometryIndex >= m_VisibleGeometrySourceIndices.size())
		return 0;

	return SkinnedMesh::GetIndexID(m_VisibleGeometrySourceIndices[geometryIndex]);
}

void DismemberMesh::Update(nvrhi::ICommandList* commandList)
{
	SkinnedMesh::Update(commandList);

	bool visibilityChanged = false;
	const auto previousVisibility = m_PartitionVisibility;
	const auto& geometryData = m_BSTriShape->GetGeometryRuntimeData();
	Util::Geometry::GetDismemberPartitionVisibility(geometryData.skinInstance.get(), m_PartitionVisibility);

	if (previousVisibility != m_PartitionVisibility) {
		logger::info("Partitions: {} - {}", m_PartitionVisibility.size(), fmt::ptr(geometryData.skinInstance.get()));

		for (size_t i = 0; i < m_PartitionVisibility.size(); i++)
			logger::info("\tVisible[{}]: {}", i, m_PartitionVisibility[i]);

		MarkDirty(DirtyFlags::Visibility);
		visibilityChanged = true;
	}

	RefreshVisibleGeometryCache();
}

bool DismemberMesh::IsPartitionVisible(size_t index) const
{
	return index < m_PartitionVisibility.size() && m_PartitionVisibility[index] != 0;
}
