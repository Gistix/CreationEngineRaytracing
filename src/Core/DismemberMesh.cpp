#include "Core/DismemberMesh.h"

#include "Util.h"

DismemberMesh::DismemberMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList) :
	SkinnedMesh(bsTriShape, commandList)
{
	if (!bsTriShape)
		return;

	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();
	Util::Geometry::GetDismemberPartitionVisibility(geometryData.skinInstance.get(), m_PartitionVisibility);
}

bool DismemberMesh::Update()
{
	const bool poseAdvanced = SkinnedMesh::Update();

	if (!m_BSTriShape)
		return poseAdvanced;

	const auto& geometryData = m_BSTriShape->GetGeometryRuntimeData();
	Util::Geometry::GetDismemberPartitionVisibility(geometryData.skinInstance.get(), m_PartitionVisibility);

	return poseAdvanced;
}

bool DismemberMesh::IsPartitionVisible(size_t index) const
{
	return index < m_PartitionVisibility.size() && m_PartitionVisibility[index] != 0;
}
