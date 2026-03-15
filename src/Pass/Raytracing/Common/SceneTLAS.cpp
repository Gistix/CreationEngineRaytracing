#include "SceneTLAS.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	SceneTLAS::SceneTLAS(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_RaytracingData = eastl::make_unique<RaytracingData>();

		m_RaytracingBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(RaytracingData), "Raytracing Data", Constants::MAX_CB_VERSIONS));
	}

	nvrhi::IBuffer* SceneTLAS::GetRaytracingBuffer()
	{
		return m_RaytracingBuffer;
	}

	TopLevelAS& SceneTLAS::GetTopLevelAS()
	{
		return m_TopLevelAS;
	}

	void SceneTLAS::Execute(nvrhi::ICommandList* commandList)
	{
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();

		auto& settings = scene->m_Settings;

		auto cameraData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData().cameraData.getEye();
		m_RaytracingData->PixelConeSpreadAngle = std::atan((2.0f / cameraData.projMat.m[1][1]) / GetRenderer()->GetDynamicResolution().y);
		m_RaytracingData->TexLODBias = settings.RaytracingSettings.TexLODBias;

		m_RaytracingData->NumLights = static_cast<uint32_t>(sceneGraph->GetLights().size());
		m_RaytracingData->RussianRoulette = settings.RaytracingSettings.RussianRoulette;
		m_RaytracingData->Roughness = settings.MaterialSettings.Roughness;
		m_RaytracingData->Metalness = settings.MaterialSettings.Metalness;

		m_RaytracingData->Emissive = settings.LightingSettings.Emissive;
		m_RaytracingData->Effect = settings.LightingSettings.Effect;
		m_RaytracingData->Sky = settings.LightingSettings.Sky;

		auto tes = RE::TES::GetSingleton();
		auto worldSpace = tes->GetRuntimeData2().worldSpace;

		if (worldSpace != nullptr) {
			auto tesDataHandler = RE::TESDataHandler::GetSingleton();
			for (auto& region : *tesDataHandler->regionList)
			{
				if (region->worldSpace == worldSpace) {
					m_RaytracingData->EmittanceColor = Util::Math::Float3(region->emittanceColor);
					break;
				}
			}
		}
		else
			m_RaytracingData->EmittanceColor = float3(1.0f, 1.0f, 1.0f);

		m_RaytracingData->Directional = settings.LightSettings.Directional;
		m_RaytracingData->Point = settings.LightSettings.Point;

		// Directional Light
		{
			//auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(RE::DrawWorld::GetSingleton().mainShadowSceneNode->GetRuntimeData().sunLight->light.get());
			auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]->GetRuntimeData().sunLight->light.get());

			auto direction = Util::Math::Float3(dirLight->GetWorldDirection());
			direction.Normalize();

			auto& diffuse = dirLight->GetLightRuntimeData().diffuse;

			m_RaytracingData->DirectionalLight.Vector = -direction;
			m_RaytracingData->DirectionalLight.Color = float3(diffuse.red, diffuse.green, diffuse.blue);
		}

		commandList->writeBuffer(m_RaytracingBuffer, m_RaytracingData.get(), sizeof(RaytracingData));

		m_TopLevelAS.Update(commandList, sceneGraph->GetInstances());
	}
}