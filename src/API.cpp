#include "API.h"
#include "Scene.h"
#include "Renderer.h"

bool Initialize(ID3D12Device5* device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue)
{
	return Renderer::GetSingleton()->Initialize(device, commandQueue, computeCommandQueue, copyCommandQueue);
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