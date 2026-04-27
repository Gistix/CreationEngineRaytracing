#pragma once

#include "Renderer.h"
#include "Core/Instance.h"
#include "Renderer.h"
#include "Scene.h"
#include "Events/ITLASUpdateListener.h"

class TopLevelAS
{
	nvrhi::rt::AccelStructHandle m_Handle;
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
		return m_Handle;
	}

	void AddListener(ITLASUpdateListener* listener)
	{
		m_Listeners.push_back(listener);
	}

	void RemoveListener(ITLASUpdateListener* listener)
	{
		eastl::erase(m_Listeners, listener);
	}

	void Update(nvrhi::ICommandList* commandList, const eastl::vector<eastl::unique_ptr<Instance>>& instances)
	{
		m_InstanceDescs.clear();
		m_InstanceDescs.reserve(instances.size());

		commandList->beginMarker("BLAS Update");

		auto* scene = Scene::GetSingleton();
		bool cullRefraction = scene->IsPathTracingActive();

		for (auto& instance : instances)
		{
			if (instance->IsHidden())
				continue;

			if (instance->SkipAS())
				continue;

			// Skip non-effect models with kRefraction when Path Tracing is active
			if (cullRefraction &&
				instance->model->GetShaderFlags().any(RE::BSShaderProperty::EShaderPropertyFlag::kRefraction) &&
				!(instance->model->GetShaderTypes() & RE::BSShader::Type::Effect))
				continue;

			instance->model->UpdateBLAS(commandList);

			m_InstanceDescs.push_back(instance->GetInstanceDesc());
		}

		commandList->endMarker();

		uint32_t topLevelInstances = static_cast<uint32_t>(m_InstanceDescs.size());

		if (scene->GetSceneGraph()->GetNumInstancesFrame() != topLevelInstances)
			logger::critical("TopLevelAS::UpdateInstance - Mismatch in number of instances and TLAS instances.");

		if (!m_Handle || topLevelInstances > m_NumInstances - Constants::TLAS_INSTANCES_THRESHOLD) {
			float topLevelInstancesRatio = std::ceil(topLevelInstances / static_cast<float>(Constants::TLAS_INSTANCES_STEP));

			uint32_t topLevelMaxInstances = static_cast<uint32_t>(topLevelInstancesRatio) * Constants::TLAS_INSTANCES_STEP;

			m_NumInstances = std::max(topLevelMaxInstances + Constants::TLAS_INSTANCES_STEP, Constants::TLAS_INSTANCES_MIN);

			logger::debug("TopLevelAS::UpdateInstance - TLAS Max Instances: {}", m_NumInstances);

			nvrhi::rt::AccelStructDesc tlasDesc;
			tlasDesc.isTopLevel = true;
			tlasDesc.topLevelMaxInstances = m_NumInstances;
			tlasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
			m_Handle = Renderer::GetSingleton()->GetDevice()->createAccelStruct(tlasDesc);

			NotifyResized();
		}

		commandList->beginMarker("TLAS Update");
		commandList->buildTopLevelAccelStruct(m_Handle, m_InstanceDescs.data(), m_InstanceDescs.size(), nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
		commandList->endMarker();
	}
};