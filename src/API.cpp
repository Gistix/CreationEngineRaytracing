#include "API.h"
#include "Scene.h"
#include "Renderer.h"

bool Initialize(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	return Scene::GetSingleton()->Initialize(RendererParams(d3d11Device, d3d12Device, commandQueue, computeCommandQueue, copyCommandQueue));
}

void ExecutePasses()
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