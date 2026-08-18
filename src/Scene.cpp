#include "Scene.h"
#include "Util.h"
#include "SceneGraph.h"

#include "Utils/Adapter.h"
#include "Utils/DXVKDetection.h"

#include "Hooks.h"

#include "framework/DescriptorTableManager.h"

#include "Renderer.h"

#include <chrono>

#include "Renderer/RenderNode.h"

#include "Pass/Raytracing/Common/Skinning.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"
#include "Pass/Raytracing/Common/SHaRCGI.h"

#include "Pass/Utility/FaceNormals.h"
#include "Pass/Utility/PostProcess.h"
#include "Pass/Raytracing/GlobalIllumination.h"
#include "Pass/Raytracing/GBuffer.h"
#include "Pass/Raytracing/PathTracing.h"
#include "Pass/Raytracing/ReSTIRGIPass.h"
#include "Pass/Raytracing/Debug.h"
#include "Pass/Raster/GBuffer.h"
#include "Pass/NRD/NRDIntegration.h"
#include "Pass/Raytracing/Common/Accumulation.h"
#include "Pass/Raytracing/Common/GIComposite.h"
#include "Pass/Raytracing/Common/LandLODOccluder.h"
#include "Pass/Raytracing/Common/TransformComposition.h"
#include "Pass/Raytracing/Common/PTComposite.h"

Scene::Scene()
{
	m_SceneGraph = eastl::make_unique<SceneGraph>();
}

void Scene::Load()
{
	Hooks::InstallEarly();

	m_IsDXVK = Util::DXVK::IsRunning();
	if (m_IsDXVK)
		logger::info("DXVK detected via d3d11.dll/dxgi.dll proxy - Switching to Vulkan mode.");
}

void Scene::PostPostLoad()
{
	Hooks::Install();
}

void Scene::DataLoaded()
{
	m_INISettings.Initialize();
}

void Scene::SetLogLevel(spdlog::level::level_enum a_level)
{
	logLevel = a_level;

	spdlog::set_level(logLevel);
	spdlog::flush_on(logLevel);

	logger::info("Log Level set to {} ({})", magic_enum::enum_name(logLevel), magic_enum::enum_integer(logLevel));
}

spdlog::level::level_enum Scene::GetLogLevel()
{
	return logLevel;
}

SceneGraph* Scene::GetSceneGraph() const
{
	return m_SceneGraph.get();
}

void Scene::UpdateMode(Mode mode)
{
	auto* renderGraph = Renderer::GetSingleton()->GetRenderGraph();
	renderGraph->ClearNodes();

	if (mode == Mode::None)
		return;

	auto* renderer = Renderer::GetSingleton();

	if (mode == Mode::GlobalIllumination) {
		auto skinning = eastl::make_unique<Pass::Skinning>(renderer);
		auto landLod = eastl::make_unique<Pass::LandLODOccluder>(renderer);
		auto transformComp = eastl::make_unique<Pass::TransformComposition>(renderer);

		auto sceneTLAS = eastl::make_unique<Pass::SceneTLAS>(renderer);
		auto* tlasPtr = sceneTLAS.get();

		auto faceNormals = eastl::make_unique<Pass::Utility::FaceNormals>(renderer);

		auto sharc = eastl::make_unique<Pass::Raytracing::Common::SHaRCGI>(renderer, tlasPtr);

		auto giPass = eastl::make_unique<Pass::Raytracing::GlobalIllumination>(renderer, tlasPtr, sharc.get());
		auto postProcess = eastl::make_unique<Pass::Utility::PostProcess>(renderer, Mode::GlobalIllumination, tlasPtr);
		auto nrdReblurPass = eastl::make_unique<Pass::NRD::NRDIntegration>(renderer, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR, Mode::GlobalIllumination);
		auto nrdRelaxPass = eastl::make_unique<Pass::NRD::NRDIntegration>(renderer, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR, Mode::GlobalIllumination);
		auto giComposite = eastl::make_unique<Pass::Common::GIComposite>(renderer, tlasPtr);

		renderGraph->AddNode({ true, "Skinning", eastl::move(skinning) });
		renderGraph->AddNode({ true, "LandLOD Occluder", eastl::move(landLod) });
		renderGraph->AddNode({ true, "Transform Composition", eastl::move(transformComp) });

		renderGraph->AddNode({ true, "Scene TLAS", eastl::move(sceneTLAS) });
		renderGraph->AddNode({ true, "Face Normals", eastl::move(faceNormals) });
		renderGraph->AddNode({ true, "SHaRC", eastl::move(sharc) });
		renderGraph->AddNode({ true, "Global Illumination", eastl::move(giPass) });
		renderGraph->AddNode({ true, "Post Process", eastl::move(postProcess) });
		renderGraph->AddNode({ true, "NRD Reblur Radiance", eastl::move(nrdReblurPass) });
		renderGraph->AddNode({ true, "NRD Relax Radiance", eastl::move(nrdRelaxPass) });
		renderGraph->AddNode({ true, "GI Composite", eastl::move(giComposite) });
	}
	else if (mode == Mode::PathTracing) {
		auto skinning = eastl::make_unique<Pass::Skinning>(renderer);
		auto landLod = eastl::make_unique<Pass::LandLODOccluder>(renderer);
		auto transformComp = eastl::make_unique<Pass::TransformComposition>(renderer);
		auto sceneTLAS = eastl::make_unique<Pass::SceneTLAS>(renderer);
		auto* tlasPtr = sceneTLAS.get();

		auto sharc = eastl::make_unique<Pass::SHaRC>(renderer, tlasPtr);
		auto* sharcPtr = sharc.get();

		auto ptPass = eastl::make_unique<Pass::PathTracing>(renderer, tlasPtr, sharcPtr);
		auto restirGI = eastl::make_unique<Pass::Raytracing::ReSTIRGIPass>(renderer, tlasPtr);
		auto postProcess = eastl::make_unique<Pass::Utility::PostProcess>(renderer, Mode::PathTracing, tlasPtr);
		auto nrdReblurPass = eastl::make_unique<Pass::NRD::NRDIntegration>(renderer, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR, Mode::PathTracing);
		auto nrdRelaxPass = eastl::make_unique<Pass::NRD::NRDIntegration>(renderer, nrd::Denoiser::RELAX_DIFFUSE_SPECULAR, Mode::PathTracing);
		auto ptComposite = eastl::make_unique<Pass::Common::PTComposite>(renderer);
		auto accumulation = eastl::make_unique<Pass::Common::Accumulation>(renderer);

		renderGraph->AddNode({ true, "Skinning", eastl::move(skinning) });
		renderGraph->AddNode({ true, "LandLOD Occluder", eastl::move(landLod) });
		renderGraph->AddNode({ true, "Transform Composition", eastl::move(transformComp) });
		renderGraph->AddNode({ true, "Scene TLAS", eastl::move(sceneTLAS) });
		renderGraph->AddNode({ true, "SHaRC", eastl::move(sharc) });
		renderGraph->AddNode({ true, "PathTracing", eastl::move(ptPass) });
		renderGraph->AddNode({ true, "ReSTIRGI", eastl::move(restirGI) });
		renderGraph->AddNode({ true, "Post Process", eastl::move(postProcess) });
		renderGraph->AddNode({ true, "NRD Reblur Radiance", eastl::move(nrdReblurPass) });
		renderGraph->AddNode({ true, "NRD Relax Radiance", eastl::move(nrdRelaxPass) });
		renderGraph->AddNode({ true, "PT Composite", eastl::move(ptComposite) });
		renderGraph->AddNode({ false, "Accumulation", eastl::move(accumulation) });
	}
	else if (mode == Mode::Debug) {
		auto skinning = eastl::make_unique<Pass::Skinning>(renderer);
		auto transformComp = eastl::make_unique<Pass::TransformComposition>(renderer);
		auto sceneTLAS = eastl::make_unique<Pass::SceneTLAS>(renderer);
		auto debugPass = eastl::make_unique<Pass::Debug>(renderer, sceneTLAS.get());

		renderGraph->AddNode({ true, "Skinning", eastl::move(skinning) });
		renderGraph->AddNode({ true, "Transform Composition", eastl::move(transformComp) });
		renderGraph->AddNode({ true, "Scene TLAS", eastl::move(sceneTLAS) });
		renderGraph->AddNode({ true, "Debug", eastl::move(debugPass) });
	}
}

void Scene::Initialize() 
{
	auto* renderer = Renderer::GetSingleton();

	// Initialize global descriptors (mesh and texture bindless arrays)
	m_SceneGraph->Initialize();

	renderer->InitDefaultTextures();

	// Camera Data
	m_CameraData = eastl::make_unique<CameraData>();
	m_CameraBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(CameraData), "Camera Data", Constants::MAX_CB_VERSIONS));

	// Feature Data
	m_FeatureData = eastl::make_unique<FeatureData>();
	m_FeatureBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(FeatureData), "Feature Data", Constants::MAX_CB_VERSIONS));
}

void Scene::Execute()
{
	if (!m_Settings.Enabled || m_Settings.GeneralSettings.Mode == Mode::None)
		return;

	auto* sceneGraph = GetSceneGraph();

	sceneGraph->UpdateCamera();

	auto* renderer = Renderer::GetSingleton();

	auto* commandList = renderer->StartExecution();

	const auto currentSlot = renderer->GetCurrentSlot();
	const auto& timings = m_Settings.DebugSettings.Timings;

	if (timings != TimingMode::Disabled) {
		auto cpuStart = std::chrono::high_resolution_clock::now();

		if (!renderer->GetFrameTimerQuery(currentSlot))
			renderer->GetFrameTimerQuery(currentSlot) = renderer->GetDevice()->createTimerQuery();

		commandList->beginTimerQuery(renderer->GetFrameTimerQuery(currentSlot));

		// Update all scene related data and their buffers
		sceneGraph->Update(commandList);

		commandList->writeBuffer(m_CameraBuffer, m_CameraData.get(), sizeof(CameraData));
		commandList->writeBuffer(m_FeatureBuffer, m_FeatureData.get(), sizeof(FeatureData));

		// Executes attached render nodes
		renderer->GetRenderGraph()->Execute(commandList);

		commandList->endTimerQuery(renderer->GetFrameTimerQuery(currentSlot));

		auto cpuEnd = std::chrono::high_resolution_clock::now();
		renderer->SetFrameCpuTime(currentSlot, std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count());
	} else {
		// Update all scene related data and their buffers
		sceneGraph->Update(commandList);

		commandList->writeBuffer(m_CameraBuffer, m_CameraData.get(), sizeof(CameraData));
		commandList->writeBuffer(m_FeatureBuffer, m_FeatureData.get(), sizeof(FeatureData));

		// Executes attached render nodes
		renderer->GetRenderGraph()->Execute(commandList);
	}

	renderer->EndExecution();
}

void Scene::UpdateCameraData() const
{
#if defined(SKYRIM)
	auto& runtimeData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();

	auto cameraData = runtimeData.cameraData.getEye();

	m_CameraData->PrevViewInverse = m_CameraData->ViewInverse;

	m_CameraData->ViewInverse = cameraData.viewMat.Invert();
	m_CameraData->ProjInverse = cameraData.projMat.Invert();

	m_CameraData->CameraData = Util::Game::GetClippingData();

	float2 ndcToViewMult = float2(2.0f / cameraData.projMat(0, 0), -2.0f / cameraData.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / cameraData.projMat(0, 0), 1.0f / cameraData.projMat(1, 1));

	m_CameraData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

	m_CameraData->Position = Util::Math::Float3(runtimeData.posAdjust.getEye());

	auto* renderer = Renderer::GetSingleton();

	m_CameraData->FrameIndex = renderer->GetFrameIndex() % UINT_MAX;
	m_CameraData->ScreenSize = renderer->GetResolution();
	m_CameraData->RenderSize = renderer->GetDynamicResolution();

	m_CameraData->PositionPrev = Util::Math::Float3(runtimeData.previousPosAdjust.getEye());

	// Used by water FlowMap
	if (g_Time)
		m_CameraData->Time = *g_Time;

	// Used by raster gbuffer
	m_CameraData->ViewProj = cameraData.viewProjMatrixUnjittered;
	m_CameraData->PrevViewProj = cameraData.previousViewProjMatrixUnjittered;

	m_CameraData->Jitter = renderer->GetJitter();

	// Actually "cameraUnderwater"?
	m_CameraData->IsUnderwater = RE::TESWaterSystem::GetSingleton()->playerUnderwater;

	// Compute underwater absorption from the current water type
	m_CameraData->UnderwaterAbsorption = float3(0.0f, 0.0f, 0.0f);
	if (m_CameraData->IsUnderwater) {
		auto* waterSystem = RE::TESWaterSystem::GetSingleton();
		if (waterSystem && waterSystem->currentWaterType) {
			float3 waterColor = Util::Math::Float3(waterSystem->currentWaterType->data.shallowWaterColor) / 255.0f;
			m_CameraData->UnderwaterAbsorption = float3(
				-std::log(std::max(waterColor.x, 1e-4f)),
				-std::log(std::max(waterColor.y, 1e-4f)),
				-std::log(std::max(waterColor.z, 1e-4f))) / Constants::WATER_ABSORPTION_REFERENCE_DEPTH * m_Settings.WaterSettings.AbsorptionScale;
		}
	}

	// Populate per-cell water data (5x5 grid centered on camera)
	{
		auto* tes = RE::TES::GetSingleton();
		auto* sky = RE::Sky::GetSingleton();
		auto eyePos = runtimeData.posAdjust.getEye();

		for (int ky = -2; ky <= 2; ky++) {
			for (int kx = -2; kx <= 2; kx++) {
				int waterTile = (kx + 2) + ((ky + 2) * 5);

				float4 data = float4(1.0f, 1.0f, 1.0f, -FLT_MAX);

				RE::NiPoint3 samplePos;
				samplePos.x = eyePos.x + static_cast<float>(kx) * 4096.0f;
				samplePos.y = eyePos.y + static_cast<float>(ky) * 4096.0f;
				samplePos.z = eyePos.z;

				if (tes) {
					if (auto* cell = tes->GetCell(samplePos)) {
						auto* extraWater = cell->extraList.GetByType<RE::ExtraCellWaterType>();
						RE::TESWaterForm* water = extraWater ? extraWater->water : nullptr;
						if (!water) {
							if (auto* worldSpace = tes->GetRuntimeData2().worldSpace) {
								water = worldSpace->worldWater;
							}
						}
						if (water) {
							data.x = (static_cast<float>(water->data.deepWaterColor.red) + static_cast<float>(water->data.shallowWaterColor.red)) / 255.0f * 0.5f;
							data.y = (static_cast<float>(water->data.deepWaterColor.green) + static_cast<float>(water->data.shallowWaterColor.green)) / 255.0f * 0.5f;
							data.z = (static_cast<float>(water->data.deepWaterColor.blue) + static_cast<float>(water->data.shallowWaterColor.blue)) / 255.0f * 0.5f;
						}

						if (sky) {
							const auto& wMul = sky->skyColor[RE::TESWeather::ColorTypes::kWaterMultiplier];
							data.x *= wMul.red;
							data.y *= wMul.green;
							data.z *= wMul.blue;
						}

						data.w = cell->GetExteriorWaterHeight() - eyePos.z;
					}
				}

				m_CameraData->WaterData[waterTile] = data;
			}
		}
	}
#elif defined(FALLOUT4)
	auto state = RE::BSGraphics::State::GetSingleton();
	auto& camViewData = state.cameraState.camViewData;

	auto viewMat = reinterpret_cast<const float4x4&>(camViewData.viewMat);
	auto projMat = reinterpret_cast<const float4x4&>(camViewData.projMat);

	m_CameraData->ViewInverse = viewMat.Invert();
	m_CameraData->ProjInverse = projMat.Invert();

	m_CameraData->CameraData = Util::Game::GetClippingData();

	float2 ndcToViewMult = float2(2.0f / projMat(0, 0), -2.0f / projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / projMat(0, 0), 1.0f / projMat(1, 1));

	m_CameraData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

	m_CameraData->Position = Util::Math::Float3(state.cameraState.posAdjust);

	auto* renderer = Renderer::GetSingleton();

	m_CameraData->FrameIndex = renderer->GetFrameIndex() % UINT_MAX;
	m_CameraData->ScreenSize = renderer->GetResolution();
	m_CameraData->RenderSize = renderer->GetDynamicResolution();

	m_CameraData->PositionPrev = Util::Math::Float3(state.cameraState.previousPosAdjust);

	if (g_Time)
		m_CameraData->Time = *g_Time;

	m_CameraData->ViewProj = reinterpret_cast<const float4x4&>(camViewData.viewProjUnjittered);
	m_CameraData->PrevViewProj = reinterpret_cast<const float4x4&>(camViewData.previousViewProjUnjittered);
	m_CameraData->PrevViewInverse = m_CameraData->ViewInverse;

	m_CameraData->Jitter = renderer->GetJitter();

	m_CameraData->IsUnderwater = false; // TODO: Fetch from FO4 water system
	m_CameraData->UnderwaterAbsorption = float3(0.0f, 0.0f, 0.0f);
#endif
}

void Scene::UpdateFeatureData(void* data, uint32_t size)
{
	if (size != sizeof(FeatureData))
	{
		logger::error("Feature data incoming and actual struct size mismatch.");
		return;
	}

	if (std::memcmp(m_FeatureData.get(), data, sizeof(FeatureData)) == 0)
		return;

	std::memcpy(m_FeatureData.get(), data, sizeof(FeatureData));
	m_DirtyFeatureData = true;
}

void Scene::SetSkyHemisphere(ID3D12Resource* skyHemi)
{
	if (skyHemi == m_SkyHemisphereResource)
		return;

	m_SkyHemisphereResource = skyHemi;

	auto* renderer = Renderer::GetSingleton();

	auto targetDesc = skyHemi->GetDesc();

	nvrhi::TextureDesc desc{};
	desc.width = static_cast<uint32_t>(targetDesc.Width);
	desc.height = targetDesc.Height;
	desc.format = renderer->GetFormat(targetDesc.Format);
	desc.mipLevels = targetDesc.MipLevels;
	desc.arraySize = targetDesc.DepthOrArraySize;
	desc.dimension = nvrhi::TextureDimension::Texture2D;
	desc.initialState = nvrhi::ResourceStates::ShaderResource;
	desc.keepInitialState = true;
	desc.debugName = "NVRHI Sky Hemisphere Texture";

	m_SkyHemisphereTexture = renderer->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, skyHemi, desc);
}

nvrhi::ITexture* Scene::GetSkinDetailNormalTexture() const
{
	if (m_SkinDetailNormalTexture)
		return m_SkinDetailNormalTexture;
	return Renderer::GetSingleton()->GetNormalTexture();
}

nvrhi::ITexture* Scene::GetProjNoiseTexture() const
{
	if (m_ProjNoiseTexture)
		return m_ProjNoiseTexture;

	auto* projNoiseMap = Util::Adapter::GetDefaultTextureProjNoiseMap();
	if (!projNoiseMap)
		return nullptr;

	m_ProjNoiseTexture = Renderer::GetSingleton()->ShareTexture(
		Util::Adapter::GetTextureResource(projNoiseMap),
		"Projection Noise Map", 
		nvrhi::Format::UNKNOWN, 
		nvrhi::ResourceStates::ShaderResource);

	return m_ProjNoiseTexture;
}

void Scene::SetSkinDetailNormal(ID3D12Resource* skinDetailNormal)
{
	if (skinDetailNormal == m_SkinDetailNormalResource)
		return;

	m_SkinDetailNormalResource = skinDetailNormal;

	if (!skinDetailNormal) {
		m_SkinDetailNormalTexture = nullptr;
		return;
	}

	auto* renderer = Renderer::GetSingleton();

	auto targetDesc = skinDetailNormal->GetDesc();

	nvrhi::TextureDesc desc{};
	desc.width = static_cast<uint32_t>(targetDesc.Width);
	desc.height = targetDesc.Height;
	desc.format = renderer->GetFormat(targetDesc.Format);
	desc.mipLevels = targetDesc.MipLevels;
	desc.arraySize = targetDesc.DepthOrArraySize;
	desc.dimension = nvrhi::TextureDimension::Texture2D;
	desc.initialState = nvrhi::ResourceStates::ShaderResource;
	desc.keepInitialState = true;
	desc.debugName = "Skin Detail Normal Texture";

	m_SkinDetailNormalTexture = renderer->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, skinDetailNormal, desc);
}

void Scene::SetWaterFlowMap(ID3D12Resource* waterFlowMap)
{
	if (waterFlowMap == m_WaterFlowMapResource)
		return;

	m_WaterFlowMapResource = waterFlowMap;

	auto* renderer = Renderer::GetSingleton();

	auto targetDesc = waterFlowMap->GetDesc();

	nvrhi::TextureDesc desc{};
	desc.width = static_cast<uint32_t>(targetDesc.Width);
	desc.height = targetDesc.Height;
	desc.format = renderer->GetFormat(targetDesc.Format);
	desc.mipLevels = targetDesc.MipLevels;
	desc.arraySize = targetDesc.DepthOrArraySize;
	desc.dimension = nvrhi::TextureDimension::Texture2D;
	desc.initialState = nvrhi::ResourceStates::ShaderResource;
	desc.keepInitialState = true;
	desc.debugName = "NVRHI Water FlowMap Texture";

	m_WaterFlowMapTexture = renderer->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, waterFlowMap, desc);
}

void Scene::UpdateSettings(Settings settings)
{
	auto previousMode = m_Settings.GeneralSettings.Mode;

	m_Settings = settings;

	auto currentMode = settings.GeneralSettings.Mode;

	auto* renderGraph = Renderer::GetSingleton()->GetRenderGraph();

	if (currentMode != previousMode || renderGraph->GetNodes().empty())
		UpdateMode(currentMode);

	const bool nrd = (settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
		settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);

	if (currentMode == Mode::GlobalIllumination) {
		// NRDIntegration nodes gate themselves per denoiser variant in SettingsChanged
	}
	else if (currentMode == Mode::PathTracing) {
		// Accumulation only works in PathTracing mode (PT writes directly to MainTexture)
		const bool accumulation = settings.GeneralSettings.Denoiser == Denoiser::Accumulation;
		renderGraph->SetEnabled<Pass::Common::Accumulation>(accumulation);
		renderGraph->SetEnabled<Pass::Common::PTComposite>(nrd);
	}

	renderGraph->SettingsChanged(settings); 
}

float Scene::GetResolutionScale() const
{
	if (m_Settings.GeneralSettings.Mode != Mode::GlobalIllumination)
		return 1.0f;

	if (m_Settings.GeneralSettings.Denoiser != Denoiser::NRD_Reblur &&
		m_Settings.GeneralSettings.Denoiser != Denoiser::NRD_Relax)
		return 1.0f;

	return m_Settings.RaytracingSettings.ResolutionScale;
}


