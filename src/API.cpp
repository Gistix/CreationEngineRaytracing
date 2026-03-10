#include "API.h"
#include "Scene.h"
#include "Renderer.h"

bool Initialize(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	return Scene::GetSingleton()->Initialize(RendererParams(d3d11Device, d3d12Device, commandQueue, computeCommandQueue, copyCommandQueue));
}

void Update()
{
	Scene::GetSingleton()->UpdateCameraData();
}

void Execute()
{
	Renderer::GetSingleton()->ExecutePasses();
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

void AttachModel(RE::TESForm* form)
{
	auto* scene = Scene::GetSingleton();
	scene->AttachModel(form);
}

void AttachLand(RE::TESForm* form, RE::NiAVObject* root)
{
	auto* scene = Scene::GetSingleton();
	scene->AttachLand(form, root);
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

float* GetFrameTime()
{
	return Renderer::GetSingleton()->GetFrameTime();
}

void UpdateSettings(Settings settings)
{
	auto* scene = Scene::GetSingleton();
	scene->UpdateSettings(settings);
}

void GetRRInput(ID3D12Resource*& diffuseAlbedo, ID3D12Resource*& specularAlbedo, ID3D12Resource*& normalRoughness, ID3D12Resource*& specularHitDistance)
{
	auto* rrInput = Renderer::GetSingleton()->GetRRInput();

	if (rrInput) {
		diffuseAlbedo = rrInput->diffuseAlbedo->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
		specularAlbedo = rrInput->specularAlbedo->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
		normalRoughness = rrInput->normalRoughness->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
		specularHitDistance = rrInput->specularHitDistance->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
	}
	else
	{
		diffuseAlbedo = nullptr;
		specularAlbedo = nullptr;
		normalRoughness = nullptr;
		specularHitDistance = nullptr;
	}
}

void SetRenderTargets(ID3D12Resource* albedo, ID3D12Resource* normalRoughness, ID3D12Resource* gnmao)
{
	Renderer::GetSingleton()->SetRenderTargets(albedo, normalRoughness, gnmao);
}

void UpdateJitter(float2 jitter)
{
	Renderer::GetSingleton()->UpdateJitter(jitter);
}