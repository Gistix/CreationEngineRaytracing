#include "Core/Light.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"

#include "Core/DirtyFlags.h"

void Light::UpdateInstances()
{
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	auto& runtimeData = m_Light->light->GetLightRuntimeData();

	m_Instances.clear();

	for (auto& instance : sceneGraph->GetInstances())
	{
		auto center = instance->node->worldBound.center;
		float radius = instance->node->worldBound.radius;

		if (center.GetDistance(m_Light->light->world.translate) > radius + runtimeData.radius.x) 
			continue;

		m_Instances.emplace(instance.get());
	}
}

void Light::UpdateTLAS(nvrhi::ICommandList* commandList)
{
	auto* renderer = Renderer::GetSingleton();

	m_InstanceDescs.clear();

	DirtyFlags dirtyFlags = DirtyFlags::None;

	for (auto* instance : m_Instances)
	{
		dirtyFlags |= instance->GetDirtyFlags();
		m_InstanceDescs.push_back(instance->GetInstanceDesc());
	}

	bool rebuild = false;

	if (m_Instances != m_PrevInstances)
	{
		rebuild = true;
		m_PrevInstances = m_Instances;
	}

	uint32_t topLevelInstances = static_cast<uint32_t>(m_InstanceDescs.size());

	if (!m_TopLevelAS || topLevelInstances > m_TopLevelInstances - Constants::NUM_INSTANCES_THRESHOLD) {
		float topLevelInstancesRatio = std::ceil(topLevelInstances / static_cast<float>(Constants::NUM_INSTANCES_STEP));

		uint32_t topLevelMaxInstances = static_cast<uint32_t>(topLevelInstancesRatio) * Constants::NUM_INSTANCES_STEP;

		m_TopLevelInstances = std::max(topLevelMaxInstances + Constants::NUM_INSTANCES_STEP, Constants::NUM_INSTANCES_MIN);

		nvrhi::rt::AccelStructDesc tlasDesc;
		tlasDesc.isTopLevel = true;
		tlasDesc.topLevelMaxInstances = m_TopLevelInstances;

		m_TopLevelAS = renderer->GetDevice()->createAccelStruct(tlasDesc);
		m_DirtyBinding = true;

		rebuild = true;
	}

	if (dirtyFlags != DirtyFlags::None || rebuild)
	{
		commandList->beginMarker("Light TLAS Update");
		commandList->buildTopLevelAccelStruct(m_TopLevelAS, m_InstanceDescs.data(), m_InstanceDescs.size());
		commandList->endMarker();
	}

	m_LastUpdate = renderer->GetFrameIndex();
}