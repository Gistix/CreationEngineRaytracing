#pragma once

#include "Renderer.h"
#include "Core/Instance.h"
#include "Renderer.h"
#include "Core/BaseMesh.h"
#include "Core/BLASCluster.h"
#include "Scene.h"
#include "Events/ITLASUpdateListener.h"

class TopLevelAS
{
	eastl::array<nvrhi::rt::AccelStructHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_Handle;
	eastl::vector<nvrhi::rt::InstanceDesc> m_InstanceDescs;
	uint32_t m_NumInstances = 0;

	eastl::vector<ITLASUpdateListener*> m_Listeners;

	void NotifyResized()
	{
		for (auto& listener : m_Listeners)
			listener->OnTLASResized(*this);
	}

public:
	nvrhi::rt::IAccelStruct* GetHandle()
	{
		return m_Handle[Renderer::GetSingleton()->GetCurrentSlot()];
	}

	void AddListener(ITLASUpdateListener* listener)
	{
		m_Listeners.push_back(listener);
	}

	void RemoveListener(ITLASUpdateListener* listener)
	{
		eastl::erase(m_Listeners, listener);
	}

	void Update(nvrhi::ICommandList* commandList,
		const eastl::unordered_map<RE::TESObjectREFR*, eastl::unique_ptr<BLASCluster>>& ownerClusters,
		const eastl::unordered_map<RE::BSTriShape*, eastl::unique_ptr<BLASCluster>>& orphanClusters)
	{
		m_InstanceDescs.clear();
		m_InstanceDescs.reserve(ownerClusters.size() + orphanClusters.size());

		auto* scene = Scene::GetSingleton();
		const auto& markers = scene->m_Settings.DebugSettings.Markers;

		auto addCluster = [&](BLASCluster* cluster)
		{
			if (!cluster->Valid())
				return;

			m_InstanceDescs.push_back(cluster->MakeInstanceDesc());
		};

		for (const auto& [owner, cluster] : ownerClusters)
			addCluster(cluster.get());

		for (const auto& [triShape, cluster] : orphanClusters)
			addCluster(cluster.get());

		const uint32_t numInstances = scene->GetSceneGraph()->GetNumInstancesFrame();
		const uint32_t topLevelInstances = static_cast<uint32_t>(m_InstanceDescs.size());

		if (numInstances != topLevelInstances)
			logger::critical("TopLevelAS::UpdateInstance - Mismatch in number of instances ({}) and TLAS instances ({}).", numInstances, topLevelInstances);

		const auto ringSlot = Renderer::GetSingleton()->GetCurrentSlot();

		if (!m_Handle[ringSlot] || topLevelInstances > m_NumInstances - Constants::TLAS_INSTANCES_THRESHOLD) {
			float topLevelInstancesRatio = std::ceil(topLevelInstances / static_cast<float>(Constants::TLAS_INSTANCES_STEP));

			uint32_t topLevelMaxInstances = static_cast<uint32_t>(topLevelInstancesRatio) * Constants::TLAS_INSTANCES_STEP;

			m_NumInstances = std::max(topLevelMaxInstances + Constants::TLAS_INSTANCES_STEP, Constants::TLAS_INSTANCES_MIN);

			logger::debug("TopLevelAS::UpdateInstance - TLAS Max Instances: {}", m_NumInstances);

			nvrhi::rt::AccelStructDesc tlasDesc;
			tlasDesc.isTopLevel = true;
			tlasDesc.topLevelMaxInstances = m_NumInstances;
			tlasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
			m_Handle[ringSlot] = Renderer::GetSingleton()->GetDevice()->createAccelStruct(tlasDesc);

			NotifyResized();
		}

		if (markers)
			commandList->beginMarker("TLAS Update");

		commandList->buildTopLevelAccelStruct(m_Handle[ringSlot], m_InstanceDescs.data(), m_InstanceDescs.size(), nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);

		if (markers)
			commandList->endMarker();
	}
};