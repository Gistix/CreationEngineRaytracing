#include "Scene.h"
#include "Util.h"
#include "SceneGraph.h"

#include "framework/DescriptorTableManager.h"

#include "Renderer.h"

#include "Renderer/RenderNode.h"
#include "Pass/RaytracingPass.h"

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

	m_FeatureData = eastl::make_unique<FeatureData>();
	//auto* renderer = Renderer::GetSingleton();

	/*m_GlobalIllumination = eastl::make_unique<RenderNode>(
		true, "Global Illumination", nullptr, {
			{ true, "RTGI", new RaytracingPass(renderer) },
			{ false, "Composite", new GICompositePass(renderer) }
		});*/

	//m_GlobalIllumination = eastl::make_unique<RenderNode>(true, "Global Illumination");
	//m_GlobalIllumination->AddNode({ true, "RTGI", new RaytracingPass(renderer) });
	//m_GlobalIllumination->AddNode({ false, "Composite", new GICompositePass(renderer) });

	//renderer->GetRenderGraph()->AttachRootNode(m_GlobalIllumination.get());
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
			"SceneTLAS",
			eastl::make_unique<Pass::SceneTLAS>(renderer)
			});

		m_GlobalIllumination->AddNode({
			true,
			"GlobalIllumination",
			eastl::make_unique<Pass::Raytracing::GlobalIllumination>(
				renderer,
				m_GlobalIllumination->GetPass<Pass::SceneTLAS>())
			}
		);
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
			"RaytracingCommon",
			eastl::make_unique<Pass::SceneTLAS>(renderer)
			});

		m_PathTracing->AddNode({
			true,
			"LightTLAS",
			eastl::make_unique<Pass::LightTLAS>(renderer)
			});

		m_PathTracing->AddNode({
			true,
			"SHaRC",
			eastl::make_unique<Pass::SHaRC>(
				renderer,
				m_PathTracing->GetPass<Pass::SceneTLAS>(),
				m_PathTracing->GetPass<Pass::LightTLAS>()
			)
			});

		m_PathTracing->AddNode({
			true,
			"PathTracing",
			eastl::make_unique<Pass::PathTracing>(
				renderer,
				m_PathTracing->GetPass<Pass::SceneTLAS>(),
				m_PathTracing->GetPass<Pass::LightTLAS>(),
				m_PathTracing->GetPass<Pass::SHaRC>()
			)
			});
	}

	return m_PathTracing.get();
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

	if (m_DirtyFeatureData)
	{
		commandList->writeBuffer(m_FeatureBuffer, m_FeatureData.get(), sizeof(FeatureData));
		m_DirtyFeatureData = false;
	}
}

void Scene::ClearDirtyStates()
{
	GetSceneGraph()->ClearDirtyStates();
}


void Scene::AttachModel([[maybe_unused]] RE::TESForm* form) 
{
	auto* refr = form->AsReference();

	auto* baseObject = refr->GetBaseObject();

	//auto flags = baseObject->GetFormFlags();
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
			// First Person
			//rt.CreateModelInternal(refr, std::format("{}_1stPerson", name).c_str(), pNiAVObject);

			// Third Person
			//rt.CreateActorModel(player, name, player->Get3D(false));
			logger::info("[Raytracing] AttachModel - Player: {}", player->GetName());
			return;
		}
	}

	if (auto* actor = refr->As<RE::Actor>()) {
		//rt.CreateActorModel(actor, actor->GetName(), pNiAVObject);
		logger::info("[Raytracing] AttachModel - Actor: {}", actor->GetName());
	}
}

void Scene::AttachLand([[maybe_unused]] RE::TESForm* form, [[maybe_unused]] RE::NiAVObject* root) 
{

}

void Scene::AddLight(RE::BSLight* light)
{
	GetSceneGraph()->AddLight(light);
}

void Scene::RemoveLight(const RE::NiPointer<RE::BSLight>& light)
{
	GetSceneGraph()->RemoveLight(light.get());
}

void Scene::UpdateCameraData() const
{
	auto& runtimeData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();

	auto cameraData = runtimeData.cameraData.getEye();

	float2 ndcToViewMult = float2(2.0f / cameraData.projMat(0, 0), -2.0f / cameraData.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / cameraData.projMat(0, 0), 1.0f / cameraData.projMat(1, 1));

	m_CameraData->PrevViewInverse = m_CameraData->ViewInverse;

	m_CameraData->ViewInverse = cameraData.viewMat.Invert();
	m_CameraData->ProjInverse = cameraData.projMat.Invert();
	m_CameraData->CameraData = Util::Game::GetClippingData();
	m_CameraData->NDCToView = float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y);
	m_CameraData->Position = Util::Math::Float3(runtimeData.posAdjust.getEye());

	auto* renderer = Renderer::GetSingleton();

	m_CameraData->FrameIndex = renderer->GetFrameIndex() % UINT_MAX;
	m_CameraData->ScreenSize = renderer->GetResolution();
	m_CameraData->RenderSize = renderer->GetDynamicResolution();

	m_CameraData->PositionPrev = Util::Math::Float3(runtimeData.previousPosAdjust.getEye());

	m_CameraData->ViewProj = cameraData.viewProjMatrixUnjittered;
	m_CameraData->PrevViewProj = cameraData.previousViewProjMatrixUnjittered;
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
}