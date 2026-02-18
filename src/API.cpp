#include "API.h"
#include "State.h"

void Initialize(ID3D12Device5* device, ID3D12CommandQueue* commandQueue)
{
	auto* state = State::GetSingleton();
	state->Initialize(device, commandQueue);
}

void UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position)
{
	auto* state = State::GetSingleton();
	state->UpdateFrameBuffer(viewInverse, projInverse, cameraData, NDCToView, position);
}

void AttachModel(RE::TESForm* form)
{
	auto* state = State::GetSingleton();
	state->AttachModel(form);
}

void AttachLand(RE::TESForm* form, RE::NiAVObject* root)
{
	auto* state = State::GetSingleton();
	state->AttachLand(form, root);
}