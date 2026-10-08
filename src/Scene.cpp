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
#include "Pass/Raytracing/Common/InstanceLightCulling.h"
#include "Pass/Raytracing/Common/PTComposite.h"

#include "Utils/DXVKInterop.h"

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

		auto instanceLightCulling = eastl::make_unique<Pass::InstanceLightCulling>(renderer);

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
		renderGraph->AddNode({ true, "Instance Light Culling", eastl::move(instanceLightCulling) });
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

		auto instanceLightCulling = eastl::make_unique<Pass::InstanceLightCulling>(renderer);

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
		renderGraph->AddNode({ true, "Instance Light Culling", eastl::move(instanceLightCulling) });
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

	CreateSobolBuffer();
}

void Scene::CreateSobolBuffer()
{
	static constexpr uint32_t directions[4][32] = {
		// Dimension 1
		{
			0x80000000u, 0xC0000000u, 0xA0000000u, 0xF0000000u,
			0x88000000u, 0xCC000000u, 0xAA000000u, 0xFF000000u,
			0x80800000u, 0xC0C00000u, 0xA0A00000u, 0xF0F00000u,
			0x88880000u, 0xCCCC0000u, 0xAAAA0000u, 0xFFFF0000u,
			0x80008000u, 0xC000C000u, 0xA000A000u, 0xF000F000u,
			0x88008800u, 0xCC00CC00u, 0xAA00AA00u, 0xFF00FF00u,
			0x80808080u, 0xC0C0C0C0u, 0xA0A0A0A0u, 0xF0F0F0F0u,
			0x88888888u, 0xCCCCCCCCu, 0xAAAAAAAAu, 0xFFFFFFFFu
		},
		// Dimension 2
		{
			0x80000000u, 0xC0000000u, 0x60000000u, 0x90000000u,
			0xE8000000u, 0x5C000000u, 0x8E000000u, 0xC5000000u,
			0x68800000u, 0x9CC00000u, 0xEE600000u, 0x55900000u,
			0x80680000u, 0xC09C0000u, 0x60EE0000u, 0x90550000u,
			0xE8808000u, 0x5CC0C000u, 0x8E606000u, 0xC5909000u,
			0x6868E800u, 0x9C9C5C00u, 0xEEEE8E00u, 0x5555C500u,
			0x8000E880u, 0xC0005CC0u, 0x60008E60u, 0x9000C590u,
			0xE8006868u, 0x5C009C9Cu, 0x8E00EEEEu, 0xC5005555u
		},
		// Dimension 3
		{
			0x80000000u, 0xC0000000u, 0x20000000u, 0x50000000u,
			0xF8000000u, 0x74000000u, 0xA2000000u, 0x93000000u,
			0xD8800000u, 0x25400000u, 0x59E00000u, 0xE6D00000u,
			0x78080000u, 0xB40C0000u, 0x82020000u, 0xC3050000u,
			0x208F8000u, 0x51474000u, 0xFBEA2000u, 0x75D93000u,
			0xA0858800u, 0x914E5400u, 0xDBE79E00u, 0x25DB6D00u,
			0x58800080u, 0xE54000C0u, 0x79E00020u, 0xB6D00050u,
			0x800800F8u, 0xC00C0074u, 0x200200A2u, 0x50050093u
		},
		// Dimension 4
		{
			0x80000000u, 0x40000000u, 0x20000000u, 0xB0000000u,
			0xF8000000u, 0xDC000000u, 0x7A000000u, 0x9D000000u,
			0x5A800000u, 0x2FC00000u, 0xA1600000u, 0xF0B00000u,
			0xDA880000u, 0x6FC40000u, 0x81620000u, 0x40BB0000u,
			0x22878000u, 0xB3C9C000u, 0xFB65A000u, 0xDDB2D000u,
			0x78022800u, 0x9C0B3C00u, 0x5A0FB600u, 0x2D0DDB00u,
			0xA2878080u, 0xF3C9C040u, 0xDB65A020u, 0x6DB2D0B0u,
			0x800228F8u, 0x400B3CDCu, 0x200FB67Au, 0xB00DDB9Du
		}
	};

	constexpr uint32_t numIndices = 65536;
	constexpr uint32_t numDims = 4;
	m_SobolData.resize(numIndices * numDims);

	for (uint32_t d = 0; d < numDims; d++) {
		for (uint32_t i = 0; i < numIndices; i++) {
			uint32_t val = 0;
			for (uint32_t bit = 0; bit < 32; bit++) {
				if ((i >> bit) & 1u)
					val ^= directions[d][bit];
			}
			m_SobolData[i + numIndices * d] = val;
		}
	}

	nvrhi::BufferDesc desc;
	desc.byteSize = numIndices * numDims * sizeof(uint32_t);
	desc.structStride = sizeof(uint32_t);
	desc.canHaveRawViews = false;
	desc.canHaveTypedViews = false;
	desc.canHaveUAVs = false;
	desc.initialState = nvrhi::ResourceStates::ShaderResource;
	desc.keepInitialState = true;
	desc.debugName = "Precomputed Sobol Buffer";

	m_SobolBuffer = Renderer::GetSingleton()->GetDevice()->createBuffer(desc);
	m_NeedsSobolUpload = true;
}

void Scene::Execute()
{
	if (!m_Settings.Enabled || m_Settings.GeneralSettings.Mode == Mode::None)
		return;

	auto* sceneGraph = GetSceneGraph();

	sceneGraph->UpdateCamera();

	auto* renderer = Renderer::GetSingleton();

	auto* commandList = renderer->StartExecution();
	m_CameraData->RenderSize = renderer->GetDynamicResolution();

	if (m_NeedsSobolUpload) {
		commandList->beginTrackingBufferState(m_SobolBuffer, nvrhi::ResourceStates::Common);
		commandList->writeBuffer(m_SobolBuffer, m_SobolData.data(), m_SobolData.size() * sizeof(uint32_t));
		commandList->setPermanentBufferState(m_SobolBuffer, nvrhi::ResourceStates::ShaderResource);
		m_NeedsSobolUpload = false;
		m_SobolData.clear();
		m_SobolData.shrink_to_fit();
	}

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

		renderer->RenderTargetManager().CopySharedTextures(commandList);

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

		renderer->RenderTargetManager().CopySharedTextures(commandList);
	}

	renderer->EndExecution();
}

void Scene::UpdateCameraData() const
{
#if defined(FALLOUT4)
	m_PrevCameraRuntimeData = m_CameraRuntimeData;
#endif
	m_CameraRuntimeData = Util::Adapter::GetCameraRuntimeData();

	m_CameraData->PrevViewInverse = m_CameraData->ViewInverse;

	m_CameraData->ViewInverse = m_CameraRuntimeData.viewMat.Invert();
	m_CameraData->ProjInverse = m_CameraRuntimeData.projMat.Invert();

	m_CameraData->CameraData = Util::Game::GetClippingData();

	float2 ndcToViewMult = float2(2.0f / m_CameraRuntimeData.projMat(0, 0), -2.0f / m_CameraRuntimeData.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / m_CameraRuntimeData.projMat(0, 0), 1.0f / m_CameraRuntimeData.projMat(1, 1));

	m_CameraData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);

	m_CameraData->Position = m_CameraRuntimeData.posAdjust;

#if defined(FALLOUT4)
	// Fallout 4 does not maintain previousViewProj/previousPosAdjust reliably
	m_CameraData->PositionPrev = m_PrevCameraRuntimeData.posAdjust;
	m_CameraData->PrevViewProj = m_PrevCameraRuntimeData.viewProjMatrixUnjittered;
#else
	m_CameraData->PositionPrev = m_CameraRuntimeData.previousPosAdjust;
	m_CameraData->PrevViewProj = m_CameraRuntimeData.previousViewProjMatrixUnjittered;
#endif

	auto* renderer = Renderer::GetSingleton();

	m_CameraData->FrameIndex = renderer->GetFrameIndex() % UINT_MAX;
	m_CameraData->ScreenSize = renderer->GetResolution();
	m_CameraData->RenderSize = renderer->GetDynamicResolution();

#if defined(SKYRIM)
	// Used by water FlowMap
	if (g_Time)
		m_CameraData->Time = *g_Time;
#elif defined(FALLOUT4)
	const auto& shaderState = Util::Adapter::GetShaderManagerState();
	m_CameraData->Time = *reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(&shaderState) + 0x28);
#endif

	// Used by raster gbuffer
	m_CameraData->ViewProj = m_CameraRuntimeData.viewProjMatrixUnjittered;

	m_CameraData->Jitter = renderer->GetJitter();

#if defined(SKYRIM)
	// Actually "cameraUnderwater"?
	m_CameraData->IsUnderwater = RE::TESWaterSystem::GetSingleton()->playerUnderwater;

	m_CameraData->UnderwaterColor = float3(1.0f, 1.0f, 1.0f);
	if (m_CameraData->IsUnderwater) {
		auto* waterSystem = RE::TESWaterSystem::GetSingleton();
		if (waterSystem && waterSystem->currentWaterType) {
			m_CameraData->UnderwaterColor = Util::Math::Float3(waterSystem->currentWaterType->data.shallowWaterColor) / 255.0f;
		}
	}

	// Populate per-cell water data (5x5 grid centered on camera)
	{
		auto* tes = RE::TES::GetSingleton();
		auto* sky = RE::Sky::GetSingleton();
		auto eyePos = m_CameraRuntimeData.posAdjust;

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
	m_CameraData->IsUnderwater = false; // TODO: Fetch from FO4 water system
	m_CameraData->UnderwaterColor = float3(1.0f, 1.0f, 1.0f);
#endif
}

void Scene::UpdateFeatureData(void* data, uint32_t size)
{
	if (!data || size != sizeof(FeatureData)) {
		logger::error("Feature data incoming and actual struct size mismatch: received {}, expected {}.", size, sizeof(FeatureData));
		return;
	}

	FeatureData incoming;
	std::memcpy(&incoming, data, sizeof(incoming));
	const auto& previous = m_FeatureData->LinearLighting;
	const auto& current = incoming.LinearLighting;
	if (current.resetHistory ||
		current.enableLinearLighting != previous.enableLinearLighting ||
		current.enableACEScg != previous.enableACEScg ||
		current.isMainOrLoadingMenu != previous.isMainOrLoadingMenu ||
		std::memcmp(&current.vanillaDiffuseColorMult, &previous.vanillaDiffuseColorMult,
			offsetof(LinearLightingSettings, directionalLightColor) - offsetof(LinearLightingSettings, vanillaDiffuseColorMult)) != 0)
		++m_LightingRevision;

	if (std::memcmp(m_FeatureData.get(), data, sizeof(FeatureData)) == 0)
		return;

	*m_FeatureData = incoming;
	m_DirtyFeatureData = true;
}

void Scene::SetSkyHemisphere(void* skyHemi)
{
	if (skyHemi == m_SkyHemisphereResource)
		return;

	m_SkyHemisphereResource = skyHemi;

	m_SkyHemisphereTexture = Renderer::WrapNativeTexture(skyHemi, "NVRHI Sky Hemisphere Texture");
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

	m_ProjNoiseTexture = Renderer::GetSingleton()->ShareTexture(Util::Adapter::GetTextureResource(projNoiseMap), "Projection Noise Map");

	return m_ProjNoiseTexture;
}

void Scene::SetSkinDetailNormal(void* skinDetailNormal)
{
	if (skinDetailNormal == m_SkinDetailNormalResource)
		return;

	auto texture = skinDetailNormal ? Renderer::WrapNativeTexture(skinDetailNormal, "NVRHI Skin Detail Normal Texture") : nullptr;
	if (skinDetailNormal && !texture)
		return;

	auto* renderer = Renderer::GetSingleton();
	if (m_SkinDetailNormalTexture && !renderer->GetDevice()->waitForIdle())
		return;

	m_SkinDetailNormalTexture = texture;
	m_SkinDetailNormalOwner.copy_from(static_cast<IUnknown*>(skinDetailNormal));
	m_SkinDetailNormalResource = skinDetailNormal;

	for (auto& node : renderer->GetRenderGraph()->GetNodes()) {
		if (node.m_RenderPass)
			node.m_RenderPass->SceneTexturesChanged();
	}
}

void Scene::SetWaterFlowMap(void* waterFlowMap)
{
	if (waterFlowMap == m_WaterFlowMapResource)
		return;

	m_WaterFlowMapResource = waterFlowMap;

	m_WaterFlowMapTexture = Renderer::WrapNativeTexture(waterFlowMap, "NVRHI Water FlowMap Texture");
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

	Renderer::GetSingleton()->SettingsChanged(settings);
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

void Scene::ReloadShaders()
{
	auto* renderGraph = Renderer::GetSingleton()->GetRenderGraph();

	for (auto& node: renderGraph->GetNodes())
	{
		auto* renderPass = node.GetPass<RenderPass>();
		if (!renderPass)
			continue;

		renderPass->ReloadShaders();
	}
}

#if defined(FALLOUT4)
void Scene::TryShareBuffer(REX::W32::ID3D11Buffer* a_buffer)
{
	auto buffer = reinterpret_cast<ID3D11Buffer*>(a_buffer);

	std::scoped_lock mutex(m_BufferMutex);
	auto [it, emplaced] = m_Buffers.try_emplace(buffer, nullptr);

	// Already shared and in the map
	if (!emplaced)
		return;

	Util::CreateSharedBuffer(buffer, it->second.put());
}

ID3D12Resource* Scene::GetSharedBuffer(REX::W32::ID3D11Buffer* a_buffer)
{
	auto buffer = reinterpret_cast<ID3D11Buffer*>(a_buffer);

	std::scoped_lock mutex(m_BufferMutex);

	auto it = m_Buffers.find(buffer);
	if (it == m_Buffers.end()) {
		logger::error("Scene::GetSharedBuffer - Buffer {} not found.", fmt::ptr(buffer));
		return nullptr;
	}

	return it->second.get();
}

void Scene::TryReleaseBuffer(REX::W32::ID3D11Buffer* a_buffer)
{
	std::scoped_lock mutex(m_BufferMutex);
	m_Buffers.erase(reinterpret_cast<ID3D11Buffer*>(a_buffer));
}
#endif
