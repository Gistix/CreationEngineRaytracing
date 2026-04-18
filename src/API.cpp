#include "API.h"
#include "Scene.h"
#include "Renderer.h"

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

void SetCopyTarget(ID3D12Resource* target)
{
	Renderer::GetSingleton()->SetCopyTarget(target);
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

void GetPassTimings(eastl::vector<PassTiming>& passTimings)
{
	passTimings = Renderer::GetSingleton()->GetPassTimings();
}

void UpdateSettings(Settings settings)
{
	auto* scene = Scene::GetSingleton();
	scene->UpdateSettings(settings);
}

void GetRRInput(ID3D12Resource*& specularAlbedo, ID3D12Resource*& specularHitDistance)
{
	auto* rrInput = Renderer::GetSingleton()->GetRRInput();

	if (rrInput) {
		specularAlbedo = rrInput->specularAlbedo->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
		specularHitDistance = rrInput->specularHitDistance->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
	}
	else
	{
		logger::error("GetRRInput failed, settings both textures to nullptr.");

		specularAlbedo = nullptr;
		specularHitDistance = nullptr;
	}
}

void SetSharedTextures(ID3D12Resource* albedo, ID3D12Resource* normalRoughness, ID3D12Resource* gnmao, ID3D12Resource* diffuseAlbedo)
{
	auto* renderer = Renderer::GetSingleton();

	renderer->SetRenderTargets(albedo, normalRoughness, gnmao);
	renderer->SetDiffuseAlbedo(diffuseAlbedo);
}

void UpdateJitter(float2 jitter)
{
	Renderer::GetSingleton()->UpdateJitter(jitter);
}

void SetPTOutputTargets(ID3D12Resource* depthTarget, ID3D12Resource* mvTarget)
{
	Renderer::GetSingleton()->SetPTOutputTargets(depthTarget, mvTarget);
}