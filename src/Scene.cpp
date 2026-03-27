#include "Scene.h"
#include "Util.h"
#include "SceneGraph.h"

#include "Hooks.h"
#include "Events.h"

#include "framework/DescriptorTableManager.h"

#include "Renderer.h"

#include "Renderer/RenderNode.h"

#include "Pass/Raytracing/Common/Skinning.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"

#include "Pass/Raytracing/GlobalIllumination.h"
#include "Pass/GIComposite.h"
#include "Pass/Raytracing/GBuffer.h"
#include "Pass/Raytracing/PathTracing.h"
#include "Pass/Raster/GBuffer.h"

Scene::Scene()
{
	m_SceneGraph = eastl::make_unique<SceneGraph>();
}

void Scene::Load()
{

}

void Scene::PostPostLoad()
{
	Hooks::Install();
}

void Scene::DataLoaded()
{
	Events::Register();
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

RenderNode* Scene::GetGlobalIllumination()
{
	if (!m_GlobalIllumination) {
		auto* renderer = Renderer::GetSingleton();

		m_GlobalIllumination = eastl::make_unique<RenderNode>(true, "Global Illumination");

		m_GlobalIllumination->AddNode({
			true,
			"Skinning",
			eastl::make_unique<Pass::Skinning>(renderer)
			});

		m_GlobalIllumination->AddNode({
			true,
			"SceneTLAS",
			eastl::make_unique<Pass::SceneTLAS>(renderer)
		});

		m_GlobalIllumination->AddNode({
			true,
			"SHaRC",
			eastl::make_unique<Pass::SHaRC>(
				renderer,
				m_GlobalIllumination->GetPass<Pass::SceneTLAS>()
			)
		});

		m_GlobalIllumination->AddNode({
			true,
			"GlobalIllumination",
			eastl::make_unique<Pass::Raytracing::GlobalIllumination>(
				renderer,
				m_GlobalIllumination->GetPass<Pass::SceneTLAS>(),
				m_GlobalIllumination->GetPass<Pass::SHaRC>()
			)			
		});
	}

	return m_GlobalIllumination.get();
}

RenderNode* Scene::GetPathTracing()
{
	if (!m_PathTracing) {
		auto* renderer = Renderer::GetSingleton();

		m_PathTracing = eastl::make_unique<RenderNode>(true, "Path Tracing");

		m_PathTracing->AddNode({
			true,
			"Skinning",
			eastl::make_unique<Pass::Skinning>(renderer)
			});

		m_PathTracing->AddNode({
			true,
			"RaytracingCommon",
			eastl::make_unique<Pass::SceneTLAS>(renderer)
		});

		m_PathTracing->AddNode({
			true,
			"SHaRC",
			eastl::make_unique<Pass::SHaRC>(
				renderer,
				m_PathTracing->GetPass<Pass::SceneTLAS>()
			)
		});

		m_PathTracing->AddNode({
			true,
			"PathTracing",
			eastl::make_unique<Pass::PathTracing>(
				renderer,
				m_PathTracing->GetPass<Pass::SceneTLAS>(),
				m_PathTracing->GetPass<Pass::SHaRC>()
			)
		});
	}

	return m_PathTracing.get();
}

nvrhi::ITexture* Scene::GetFlowMapTexture()
{
	if (!g_FlowMapSourceTex->get())
		return Renderer::GetSingleton()->GetBlackTexture();

	if (!m_FlowMapTexture) {		
		auto d3d11Texture = reinterpret_cast<ID3D11Texture2D*>(g_FlowMapSourceTex->get()->rendererTexture->texture);

		winrt::com_ptr<IDXGIResource> dxgiResource;
		HRESULT hr = d3d11Texture->QueryInterface(IID_PPV_ARGS(&dxgiResource));

		if (FAILED(hr)) {
			logger::error("Scene::GetFlowMapTexture - Failed to query interface.");
			return nullptr;
		}

		HANDLE sharedHandle = nullptr;
		hr = dxgiResource->GetSharedHandle(&sharedHandle);

		if (FAILED(hr) || !sharedHandle) {
			D3D11_TEXTURE2D_DESC desc;
			d3d11Texture->GetDesc(&desc);

			logger::debug("Scene::GetFlowMapTexture - Failed to get shared handle - [{}, {}] Format: {}", desc.Width, desc.Height, magic_enum::enum_name(desc.Format));
			return nullptr;
		}

		auto* d3d12Device = Renderer::GetSingleton()->GetNativeD3D12Device();

		hr = d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(&m_FlowMapResource));

		if (FAILED(hr)) {
			logger::error("Scene::GetFlowMapTexture - Failed to open shared handle.");
			return nullptr;
		}

		if (!m_FlowMapResource) {
			logger::error("Scene::GetFlowMapTexture - Failed to adquire DX12 texture.");
			return nullptr;
		}

		D3D12_RESOURCE_DESC nativeTexDesc = m_FlowMapResource->GetDesc();
		auto formatIt = Renderer::GetFormatMapping().find(nativeTexDesc.Format);

		if (formatIt == Renderer::GetFormatMapping().end()) {
			logger::error("Scene::GetFlowMapTexture - Unmapped format {}", magic_enum::enum_name(nativeTexDesc.Format));
			return nullptr;
		}

		auto& textureDesc = nvrhi::TextureDesc()
			.setWidth(static_cast<uint32_t>(nativeTexDesc.Width))
			.setHeight(nativeTexDesc.Height)
			.setFormat(formatIt->second)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
			.setDebugName("FlowMap Texture");

		m_FlowMapTexture = Renderer::GetSingleton()->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(m_FlowMapResource), textureDesc);
	}

	return m_FlowMapTexture;
}

RenderNode* Scene::GetModeNode(Mode mode)
{
	if (mode == Mode::GlobalIllumination)
		return GetGlobalIllumination();
	else if (mode == Mode::PathTracing)
		return GetPathTracing();

	return nullptr;
}

void Scene::UpdateMode(Mode mode, Mode previousMode)
{
	auto* rootNode = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode();

	// Detach previous mode node
	rootNode->DetachRenderNode(GetModeNode(previousMode));

	// Attach new mode node
	rootNode->AttachRenderNode(GetModeNode(mode));
}

bool Scene::Initialize(RendererParams rendererParams) 
{
	auto* renderer = Renderer::GetSingleton();

	// Initialize renderer
	renderer->Initialize(rendererParams);

	if (!renderer->GetDevice())
		return false;

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


	//m_GBuffer = eastl::make_unique<RenderNode>(true, "GBuffer", eastl::make_unique<Pass::GBuffer>(renderer));

	/*{
		m_PathTracing = eastl::make_unique<RenderNode>(true, "Path Tracing");

		m_PathTracing->AddNode({
			true,
			"RaytracingCommon",
			eastl::make_unique<Pass::SceneTLAS>(renderer)
		});

		m_PathTracing->AddNode({
			true,
			"RTGBuffer",
			eastl::make_unique<Pass::Raytracing::GBuffer>(
				renderer,
				m_PathTracing->GetPass<Pass::SceneTLAS>()
			)
		});
	}*/

	return true;
}

void Scene::Render()
{
	if (!m_Settings.Enabled)
		return;

	UpdateCameraData();

	Renderer::GetSingleton()->ExecutePasses();
}

void Scene::Update(nvrhi::ICommandList* commandList)
{
	GetSceneGraph()->Update(commandList);

	// Update camera data buffer
	commandList->writeBuffer(m_CameraBuffer, m_CameraData.get(), sizeof(CameraData));

	/*if (m_DirtyFeatureData)
	{*/
		commandList->writeBuffer(m_FeatureBuffer, m_FeatureData.get(), sizeof(FeatureData));
		/*m_DirtyFeatureData = false;
	}*/
}

void Scene::ClearDirtyStates()
{
	GetSceneGraph()->ClearDirtyStates();
}


void Scene::AttachModel([[maybe_unused]] RE::TESForm* form) 
{
	auto* refr = form->AsReference();

	auto* baseObject = refr->GetBaseObject();

	RE::FormType type = baseObject->GetFormType();

	if (type == RE::FormType::IdleMarker)
		return;

	if (baseObject->IsMarker())
		return;

	auto* node = refr->Get3D();

	if (!node)
		return;

	if (auto* model = baseObject->As<RE::TESModel>()) {
		GetSceneGraph()->CreateModel(refr, model->GetModel(), node);
		return;
	}

	if (Util::IsPlayer(refr)) {
		if (auto* player = reinterpret_cast<RE::PlayerCharacter*>(refr)) {
			auto name = player->GetName();
			// First Person
			//rt.CreateModelInternal(refr, std::format("{}_1stPerson", name).c_str(), node);

			// Third Person
			GetSceneGraph()->CreateActorModel(player, name, player->Get3D(false));
			return;
		}
	}

	if (auto* actor = refr->As<RE::Actor>()) {
		GetSceneGraph()->CreateActorModel(actor, actor->GetName(), node);
	}
}

void Scene::AttachLand(RE::TESObjectLAND* land)
{
	if (!land)
		return;

	GetSceneGraph()->CreateLandModel(land);
}

void Scene::UpdateCameraData() const
{
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
	desc.debugName = "Copy Target Texture";

	m_SkyHemisphereTexture = renderer->GetDevice()->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, skyHemi, desc);
}

void Scene::UpdateSettings(Settings settings)
{
	auto previousMode = m_Settings.GeneralSettings.Mode;

	m_Settings = settings;

	auto currentMode = settings.GeneralSettings.Mode;

	auto* rootNode = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode();

	if (currentMode != previousMode || !rootNode->HasRenderNode())
		UpdateMode(currentMode, previousMode);

	rootNode->SettingsChanged(settings);
}