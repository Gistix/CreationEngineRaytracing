#pragma once

extern "C" {
	CERT_API bool Initialize(ID3D12Device5* device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);
	CERT_API void ExecutePasses();
	CERT_API void WaitExecution();
	CERT_API void AttachModel(RE::TESForm* form);
	CERT_API void AttachLand(RE::TESForm* form, RE::NiAVObject* root);
}