#include "SceneTLAS.h"
#include "Renderer.h"
#include "Scene.h"
#include "Utils/Celestial.h"

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
			m_RaytracingData->DirectionalLight.Direction = -Util::Math::Normalize(Util::Math::Float3(dirLight->GetWorldDirection()));
			// Colour comes from CS linear lighting (SkySync sets it per sun/moon). Alpha stays in Fade because the
			// non-LL path in DirLightToLinear applies the sRGB transfer to Color before the alpha is applied.
			const auto& llColor = scene->m_FeatureData->LinearLighting.directionalLightColor;
			m_RaytracingData->DirectionalLight.Color = float3(llColor.x, llColor.y, llColor.z);
			m_RaytracingData->DirectionalLight.Fade = llColor.w;
#elif defined(FALLOUT4)
			auto ssn = Util::Adapter::GetShadowSceneNode(0);
			if (ssn && ssn->sunLight && ssn->sunLight->light) {
				auto* dirLight = static_cast<RE::NiDirectionalLight*>(ssn->sunLight->light.get());
				if (dirLight) {
					auto runtimeData = Util::Adapter::GetLightRuntimeData(dirLight);
					m_RaytracingData->DirectionalLight.Direction = -Util::Math::Normalize(Util::Math::GetMatrixColumn(dirLight->world.rotate, 0));
					m_RaytracingData->DirectionalLight.Color = Util::Math::Float3(runtimeData.diffuse);
					m_RaytracingData->DirectionalLight.Fade = 1.0f;
				}
			}
#endif

			m_RaytracingData->NumDirectionalLights = 1;
			m_RaytracingData->DirectionalLight2 = m_RaytracingData->DirectionalLight;

#if defined(SKYRIM)
			// Night: one directional light per moon, coloured according to LightingSettings::CelestialColors.
			//  Default:  the brighter moon keeps the CS linear lighting colour and energy, the other moon's colour is that
			//            scaled by its base colour and intensity relative to the brighter moon.
			//  Hue:      moon base colours, brightness from the CS linear lighting colour.
			//  Absolute: moon base colours and intensities only.
			Util::Celestial::NightLights night;
			if (Util::Celestial::GetNightLights(night) && night.SunBelowHorizon) {
				const auto* primary = &night.Masser;
				const auto* secondary = &night.Secunda;
				if (secondary->Intensity > primary->Intensity)
					std::swap(primary, secondary);

				if (primary->IsLit()) {
					const auto& ll = scene->m_FeatureData->LinearLighting;
					const auto mode = settings.LightingSettings.CelestialColors;
					const bool hasSecondary = secondary->IsLit();

					const float3 gameColor(llColor.x, llColor.y, llColor.z);
					const float3 primaryColor = Util::Celestial::ToLightInputSpace(primary->Color, ll);
					const float3 secondaryColor = Util::Celestial::ToLightInputSpace(secondary->Color, ll);
					const float intensityRatio = secondary->Intensity / primary->Intensity;

					auto& light0 = m_RaytracingData->DirectionalLight;
					auto& light1 = m_RaytracingData->DirectionalLight2;

					light0.Direction = primary->Direction;
					light1.Direction = secondary->Direction;

					switch (mode) {
					case CelestialColorMode::Hue:
						{
							// Split the game light's luminance between the moons by their intensities, so the second
							// moon doesn't add energy on top of what the game/CS intended for the night.
							const float primaryWeight = Util::Celestial::WorkingLuminance(primaryColor, ll);
							const float secondaryWeight = hasSecondary ? Util::Celestial::WorkingLuminance(secondaryColor, ll) * intensityRatio : 0.0f;
							const float totalWeight = primaryWeight + secondaryWeight;
							const float scale = totalWeight > 0.0f ? Util::Celestial::WorkingLuminance(gameColor, ll) / totalWeight : 0.0f;
							light0.Color = primaryColor * scale;
							light1.Color = secondaryColor * (scale * intensityRatio);
							light0.Fade = light1.Fade = llColor.w;
						}
						break;
					case CelestialColorMode::Absolute:
						light0.Color = primaryColor;
						light0.Fade = primary->Intensity;
						light1.Color = secondaryColor;
						light1.Fade = secondary->Intensity;
						break;
					default:
						light1.Color = gameColor * (secondary->Color / primary->Color) * intensityRatio;
						break;
					}

					if (hasSecondary)
						m_RaytracingData->NumDirectionalLights = 2;
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