#include "API.h"
#include "Scene.h"

void Initialize(ID3D12Device5* device, ID3D12CommandQueue* commandQueue)
{
	auto* scene = Scene::GetSingleton();
	scene->Initialize(device, commandQueue);
}

void UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position)
{
	auto* scene = Scene::GetSingleton();
	scene->UpdateFrameBuffer(viewInverse, projInverse, cameraData, NDCToView, position);
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