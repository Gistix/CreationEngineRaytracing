#include "RaytracingCommon.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	RaytracingCommon::RaytracingCommon(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_RaytracingData = eastl::make_unique<RaytracingData>();

		m_RaytracingBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(RaytracingData), "Raytracing Data", Constants::MAX_CB_VERSIONS));
	}

	void RaytracingCommon::UpdateAccelStructs(nvrhi::ICommandList* commandList)
	{
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
		auto& instances = sceneGraph->GetInstances();

		m_InstanceDescs.clear();
		m_InstanceDescs.reserve(instances.size());

		for (auto& instance : instances)
		{
			/*if (!instance->model->blas)
				continue;*/

			m_InstanceDescs.push_back(instance->GetInstanceDesc());
		}

		// Compact acceleration structures that are tagged for compaction and have finished executing the original build
		commandList->compactBottomLevelAccelStructs();

		uint32_t topLevelInstances = static_cast<uint32_t>(m_InstanceDescs.size());

		if (!m_TopLevelAS || topLevelInstances > m_TopLevelInstances - Constants::NUM_INSTANCES_THRESHOLD) {
			float topLevelInstancesRatio = std::ceil(topLevelInstances / static_cast<float>(Constants::NUM_INSTANCES_STEP));

			uint32_t topLevelMaxInstances = static_cast<uint32_t>(topLevelInstancesRatio) * Constants::NUM_INSTANCES_STEP;

			m_TopLevelInstances = std::max(topLevelMaxInstances + Constants::NUM_INSTANCES_STEP, Constants::NUM_INSTANCES_MIN);

			nvrhi::rt::AccelStructDesc tlasDesc;
			tlasDesc.isTopLevel = true;
			tlasDesc.topLevelMaxInstances = m_TopLevelInstances;
			m_TopLevelAS = GetRenderer()->GetDevice()->createAccelStruct(tlasDesc);
		}

		commandList->beginMarker("TLAS Update");
		commandList->buildTopLevelAccelStruct(m_TopLevelAS, m_InstanceDescs.data(), m_InstanceDescs.size());
		commandList->endMarker();
	}

	nvrhi::IBuffer* RaytracingCommon::GetRaytracingBuffer()
	{
		return m_RaytracingBuffer;
	}

	nvrhi::rt::IAccelStruct* RaytracingCommon::GetTopLevelAS()
	{
		return m_TopLevelAS;
	}

	void RaytracingCommon::Execute(nvrhi::ICommandList* commandList)
	{
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();

		auto& settings = scene->settings;

		auto cameraData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData().cameraData.getEye();
		m_RaytracingData->PixelConeSpreadAngle = std::atan((2.0f / cameraData.projMat.m[1][1]) / GetRenderer()->GetRenderSize().y);
		m_RaytracingData->TexLODBias = settings.RaytracingSettings.TexLODBias;

		m_RaytracingData->NumLights = sceneGraph->GetNumActiveLights();
		m_RaytracingData->RussianRoulette = settings.RaytracingSettings.RussianRoulette;
		m_RaytracingData->Roughness = settings.MaterialSettings.Roughness;
		m_RaytracingData->Metalness = settings.MaterialSettings.Metalness;

		m_RaytracingData->Emissive = settings.LightingSettings.Emissive;
		m_RaytracingData->Effect = settings.LightingSettings.Effect;
		m_RaytracingData->Sky = settings.LightingSettings.Sky;
		m_RaytracingData->EmittanceColor = float3(1.0f, 1.0f, 1.0f);

		auto& shaderManagerState = RE::BSShaderManager::State::GetSingleton();

		// This is probably not the same as 'activeShadowSceneNode'
		auto* shadowSceneNode = shaderManagerState.shadowSceneNode[0];
		
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(shadowSceneNode->GetRuntimeData().sunLight->light.get());

		auto direction = Util::Float3(dirLight->GetWorldDirection());
		direction.Normalize();

		auto& diffuse = dirLight->GetLightRuntimeData().diffuse;

		m_RaytracingData->DirectionalLight.Vector = -direction;
		m_RaytracingData->DirectionalLight.Color = float3(diffuse.red, diffuse.green, diffuse.blue) * settings.LightSettings.Directional;

		commandList->writeBuffer(m_RaytracingBuffer, m_RaytracingData.get(), sizeof(RaytracingData));

		UpdateAccelStructs(commandList);
	}
}