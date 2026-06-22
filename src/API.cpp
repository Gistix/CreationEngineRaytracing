#include "API.h"
#include "Scene.h"
#include "Renderer.h"
#include "Pass/Raytracing/Common/Accumulation.h"

bool InitializeRenderer(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	return Renderer::GetSingleton()->Initialize(RendererParams(d3d11Device, d3d12Device, commandQueue, computeCommandQueue, copyCommandQueue));
}

void Initialize(Settings settings)
{
	auto* scene = Scene::GetSingleton();

	scene->Initialize();
	scene->UpdateSettings(settings);
}

void UpdateCamera()
{
	Scene::GetSingleton()->UpdateCameraData();
}

void Execute()
{
	Scene::GetSingleton()->Execute();
}

void SetResolution(uint32_t width, uint32_t height) {
	Renderer::GetSingleton()->SetResolution({ width, height });
}

void GetResolution(uint32_t& width, uint32_t& height)
{
	auto resolution = Renderer::GetSingleton()->GetResolution();

	width = resolution.x;
	height = resolution.y;
}

void WaitExecution()
{
	Renderer::GetSingleton()->WaitExecution();
}

void PostExecution()
{
	Renderer::GetSingleton()->PostExecution();
}

void UpdateFeatureData(void* data, uint32_t size)
{
	auto* scene = Scene::GetSingleton();
	scene->UpdateFeatureData(data, size);
}

void SetSkyHemisphere(ID3D12Resource* skyHemi)
{
	auto* scene = Scene::GetSingleton();
	scene->SetSkyHemisphere(skyHemi);
}

void SetSkinDetailNormal(ID3D12Resource* skinDetailNormal)
{
	auto* scene = Scene::GetSingleton();
	scene->SetSkinDetailNormal(skinDetailNormal);
}

void SetWaterFlowMap(ID3D12Resource* waterFlowMap)
{
	auto* scene = Scene::GetSingleton();
	scene->SetWaterFlowMap(waterFlowMap);
}

void GetPassTimings(eastl::vector<PassTiming>& passTimings)
{
	passTimings = Renderer::GetSingleton()->GetPassTimings();
}

void GetSceneGraphCounters(uint32_t& textures, uint32_t& models, uint32_t& instances)
{
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	auto& textureManager = sceneGraph->GetTextureManager();

	textures = static_cast<uint32_t>(textureManager->m_Textures.size() + textureManager->m_NormalMaps.size());
	models = static_cast<uint32_t>(sceneGraph->GetDirectMeshes().size());
	instances = static_cast<uint32_t>(sceneGraph->GetOwnerClusters().size() + sceneGraph->GetOrphanClusters().size());
}

void UpdateSettings(Settings settings)
{
	auto* scene = Scene::GetSingleton();
	scene->UpdateSettings(settings);
}

void GetRRInput(ID3D12Resource*& specularAlbedo, ID3D12Resource*& specularHitDistance)
{
	auto& textureManager = Renderer::GetSingleton()->RenderTargetManager();
	specularAlbedo = textureManager.GetTexture(RenderTarget::RRSpecularAlbedo)->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
	specularHitDistance = textureManager.GetTexture(RenderTarget::RRSpecularHitDist)->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
}

void SetSharedTextures(ID3D12Resource* albedo, ID3D12Resource* normalRoughness, ID3D12Resource* gnmao)
{
	auto* renderer = Renderer::GetSingleton();
	renderer->SetRenderTargets(albedo, normalRoughness, gnmao);
}

void GetSharedTextures(SharedTexture& depth, SharedTexture& motionVector, SharedTexture& main, SharedTexture& diffuseAlbedo)
{
	auto& textureManager = Renderer::GetSingleton()->RenderTargetManager();

	depth = textureManager.GetSharedTexture(RenderTarget::ClipDepth);
	motionVector = textureManager.GetSharedTexture(RenderTarget::MotionVectors3D);
	main = textureManager.GetSharedTexture(RenderTarget::Main);
	diffuseAlbedo = textureManager.GetSharedTexture(RenderTarget::DiffuseAlbedo);
}

void UpdateJitter(float2 jitter)
{
	Renderer::GetSingleton()->UpdateJitter(jitter);
}

uint32_t GetAccumulatedFrameCount()
{
	auto* rootNode = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode();
	auto* accumulationPass = rootNode->GetPass<Pass::Common::Accumulation>();
	if (accumulationPass)
		return accumulationPass->GetAccumulatedFrames();
	return 0;
}

uint64_t GetFakeDoubledVRAMUsage()
{
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	if (!sceneGraph)
		return 0;

	auto& textureManager = sceneGraph->GetTextureManager();

	if (!textureManager)
		return 0;

	return textureManager->GetFakeDoubledVRAMUsage();
}