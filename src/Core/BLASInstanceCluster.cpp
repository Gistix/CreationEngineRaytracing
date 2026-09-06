#include "Core/BLASInstanceCluster.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/InstanceMask.h"

BLASInstanceCluster::BLASInstanceCluster(RE::TESObjectREFR* owner) : 
	BLASCluster(owner)
{
	m_Name = { "Instance BLAS" };

	m_Transform = Constants::kIdentityTransform;
	m_PrevTransform = Constants::kIdentityTransform;
	m_NeedsPrevInit = false;
}

uint32_t BLASInstanceCluster::GetInstanceCount() const
{
	if (!Valid())
		return 0;

	return m_Members.front()->AsInstancedMesh()->GetInstanceCount();
}

uint32_t BLASInstanceCluster::Update()
{
	const uint32_t meshCount = BLASCluster::Update();

	// Multi-instance geometry should not be frustum-culled as a single point at origin
	m_Flags.reset(Flags::FrustumCulled);

	m_IsValid = (meshCount > 0) && (m_Members.front()->AsInstancedMesh()->GetInstanceCount() > 0);

	return meshCount;
}

void BLASInstanceCluster::AppendInstanceDescs(eastl::vector<nvrhi::rt::InstanceDesc>& outDescs) const
{
	if (!m_IsValid || !m_BLAS)
		return;

	const auto& instances = m_Members.front()->AsInstancedMesh()->GetInstances();
	const uint32_t instCount = static_cast<uint32_t>(instances.size());

	for (uint32_t i = 0; i < instCount; i++) {
		auto instanceDesc = nvrhi::rt::InstanceDesc()
			.setInstanceID(m_InstanceIndex + i)
			.setInstanceMask(InstanceMask::Default)
			.setTransform(instances[i].transform.f)
			.setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
			.setBLAS(m_BLAS);

		outDescs.push_back(instanceDesc);
	}
}

void BLASInstanceCluster::WriteInstanceData(uint32_t firstMesh, uint32_t meshCount, InstanceData* outInstances) const
{
	if (!outInstances)
		return;

	const auto& instances = m_Members.front()->AsInstancedMesh()->GetInstances();
	const uint32_t instCount = static_cast<uint32_t>(instances.size());

	for (uint32_t i = 0; i < instCount; i++) {
		InstanceData& data = outInstances[i];
		data.Transform = instances[i].transform;
		data.PrevTransform = instances[i].prevTransform;
		data.LightData = m_InstanceLightData;
		data.FirstGeometryID = firstMesh;
		data.NumGeometry = meshCount;
		data.Alpha = instances[i].alpha;
	}
}
