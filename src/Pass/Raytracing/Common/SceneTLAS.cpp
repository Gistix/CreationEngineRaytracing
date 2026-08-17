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

		auto& cameraData = Util::Adapter::GetCameraEyeViewData();
		m_RaytracingData->PixelConeSpreadAngle = std::atan((2.0f / reinterpret_cast<const DirectX::XMFLOAT4X4&>(cameraData.projMat).m[1][1]) / GetRenderer()->GetDynamicResolution().y);
		m_RaytracingData->TexLODBias = settings.AdvancedSettings.TexLODBias;

		m_RaytracingData->NumLights = static_cast<uint32_t>(sceneGraph->GetLights().size());
		m_RaytracingData->Roughness = settings.MaterialSettings.Roughness;
		m_RaytracingData->Metalness = settings.MaterialSettings.Metalness;

		m_RaytracingData->Directional = settings.LightingSettings.Directional;
		m_RaytracingData->Point = settings.LightingSettings.Point;
		m_RaytracingData->Emissive = settings.LightingSettings.Emissive;
		m_RaytracingData->Effect = settings.LightingSettings.Effect;
		m_RaytracingData->Sky = settings.LightingSettings.Sky;
		m_RaytracingData->WaterAbsorptionScale = settings.WaterSettings.AbsorptionScale;
		m_RaytracingData->EnableReSTIRGI = settings.ReSTIRGI.Enabled ? 1 : 0;
		m_RaytracingData->ResolutionScale = scene->GetResolutionScale();

		m_RaytracingData->NumMeshes = sceneGraph->GetNumMeshesFrame();
		m_RaytracingData->NumInstances = sceneGraph->GetNumInstancesFrame();

		// Water ObjectUV
		{
			if (scene->g_FlowMapSize && scene->g_DisplacementMeshFlowCellOffset) {
				int32_t flowMapSize = *scene->g_FlowMapSize;

				m_RaytracingData->WaterObjectUV = {
					static_cast<float>(flowMapSize),
					scene->g_DisplacementMeshFlowCellOffset->x,
					1.0f - scene->g_DisplacementMeshFlowCellOffset->y
				};
			}

			if (scene->g_DisplacementMeshPos)
				m_RaytracingData->WaterDisplacementPosition = Util::Math::Float2(*scene->g_DisplacementMeshPos);
		}

		m_RaytracingData->HitDistSettings = float4(
			3.0f * Util::Units::M_TO_GAME_UNIT,  // (units > 0) - constant value
			0.1f * Util::Units::M_TO_GAME_UNIT,  // (> 0) - viewZ based linear scale (1 m - 10 cm, 10 m - 1 m, 100 m - 10 m)
			20.0f,								 // (>= 1) - roughness based scale, use values > 1 to clamp hit distance to a larger value for low roughness
			0.0f);

		// Directional Light
		{
#if defined(SKYRIM)
			auto dirLight = ce_cast<RE::NiDirectionalLight*>(Util::Adapter::GetShaderManagerState().shadowSceneNode[0]->GetRuntimeData().sunLight->light.get());
			auto& lightRuntimeData = dirLight->GetLightRuntimeData();
			m_RaytracingData->DirectionalLight.Direction = -Util::Math::Normalize(Util::Math::Float3(dirLight->GetWorldDirection()));
			m_RaytracingData->DirectionalLight.Color = Util::Math::Float3(lightRuntimeData.diffuse);
			m_RaytracingData->DirectionalLight.Fade = lightRuntimeData.fade;
#elif defined(FALLOUT4)
			auto ssn = Util::Adapter::GetShadowSceneNode(0);
			if (ssn && ssn->sunLight && ssn->sunLight->light) {
				auto* dirLight = static_cast<RE::NiDirectionalLight*>(ssn->sunLight->light.get());
				if (dirLight) {
					auto runtimeData = Util::Adapter::GetLightRuntimeData(dirLight);
					m_RaytracingData->DirectionalLight.Direction = -Util::Math::Normalize(float3(dirLight->world.rotate.entry[0][0], dirLight->world.rotate.entry[1][0], dirLight->world.rotate.entry[2][0]));
					m_RaytracingData->DirectionalLight.Color = Util::Math::Float3(runtimeData.diffuse);
					m_RaytracingData->DirectionalLight.Fade = runtimeData.fade;
				}
			}
#endif
		}

		// SSS
		{
			auto& sssSettings = settings.AdvancedSettings.SSSSettings;
			auto& sssData = m_RaytracingData->SubSurfaceScattering;

			sssData.SampleCount = sssSettings.SampleCount;
			sssData.MaxSampleRadius = sssSettings.MaxSampleRadius;
			sssData.EnableTransmission = sssSettings.EnableTransmission;
			sssData.MaterialOverride = sssSettings.MaterialOverride;
			sssData.TransmissionColorOverride = sssSettings.OverrideTransmissionColor;
			sssData.ScatteringColorOverride = sssSettings.OverrideScatteringColor;
			sssData.ScaleOverride = sssSettings.OverrideScale;
			sssData.AnisotropyOverride = sssSettings.OverrideAnisotropy;
		}

		commandList->writeBuffer(m_RaytracingBuffer, m_RaytracingData.get(), sizeof(RaytracingData));

		sceneGraph->BuildClusters(commandList);

		m_TopLevelAS.Update(commandList, sceneGraph->GetAllClusters());
	}
}