#include "Scene.h"
#include "Util.h"
#include "SceneGraph.h"

#include "framework/DescriptorTableManager.h"

#include "Renderer.h"

#include "Renderer/RenderNode.h"
#include "Passes/RaytracingPass.h"

#include "Passes/RaytracingCommon.h"
#include "Passes/RaytracedGI.h"
#include "Passes/GIComposite.h"
#include "Passes/PathTracing.h"

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

bool Scene::Initialize(RendererParams rendererParams) {
	auto* renderer = Renderer::GetSingleton();

	// Initialize renderer
	renderer->Initialize(rendererParams);

	if (!renderer->GetDevice())
		return false;

	// Initialize global descriptors (mesh and texture bindless arrays)
	m_SceneGraph->Initialize();

	renderer->InitDefaultTextures();

	// We split render pass initialization from renderer because of the global descriptors
	renderer->InitRenderPasses();

	// Raytracing passes require a 'RaytracingCommon' pass to create and manage the TLAS
	{
		m_GlobalIllumination = eastl::make_unique<RenderNode>(true, "Global Illumination", eastl::make_unique<Pass::RaytracingCommon>(renderer));

		m_GlobalIllumination->AddNode({
			true,
			"RaytracedGI",
			eastl::make_unique<Pass::RaytracedGI>(
				renderer,
				m_GlobalIllumination->GetPass<Pass::RaytracingCommon>())
			}
		);
	}

	{
		m_PathTracing = eastl::make_unique<RenderNode>(true, "Path Tracing", eastl::make_unique<Pass::RaytracingCommon>(renderer));

		m_PathTracing->AddNode({
			true,
			"PathTracing",
			eastl::make_unique<Pass::PathTracing>(
				renderer,
				m_PathTracing->GetPass<Pass::RaytracingCommon>())
			}
		);
	}

	renderer->GetRenderGraph()->AttachRootNode(m_PathTracing.get());

	// Camera Data
	m_CameraData = eastl::make_unique<CameraData>();
	m_CameraBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(CameraData), "Camera Data", Constants::MAX_CB_VERSIONS));

	// Feature Data
	m_FeatureData = eastl::make_unique<FeatureData>();
	m_FeatureBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(FeatureData), "Feature Data", Constants::MAX_CB_VERSIONS));

	return true;
}

void Scene::Render()
{
	auto& runtimeData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();

	auto cameraData = runtimeData.cameraData.getEye();

	float2 ndcToViewMult = float2(2.0f / cameraData.projMat(0, 0), -2.0f / cameraData.projMat(1, 1));
	float2 ndcToViewAdd = float2(-1.0f / cameraData.projMat(0, 0), 1.0f / cameraData.projMat(1, 1));

	UpdateCameraData(
		cameraData.viewMat.Invert(),
		cameraData.projMat.Invert(),
		Util::Game::GetClippingData(),
		float4(ndcToViewMult.x, ndcToViewMult.y, ndcToViewAdd.x, ndcToViewAdd.y),
		Util::Float3(runtimeData.posAdjust.getEye())
	);

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

void Scene::UpdateCameraData(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const
{
	m_CameraData->ViewInverse = viewInverse;
	m_CameraData->ProjInverse = projInverse;
	m_CameraData->CameraData = cameraData;
	m_CameraData->NDCToView = NDCToView;
	m_CameraData->Position = position;

	auto* renderer = Renderer::GetSingleton();

	m_CameraData->FrameIndex = renderer->GetFrameIndex() % UINT_MAX;
	m_CameraData->RenderSize = renderer->GetRenderSize();
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