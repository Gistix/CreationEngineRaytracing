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