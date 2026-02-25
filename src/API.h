#pragma once

extern "C" {
	CERT_API bool Initialize(ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);
	CERT_API void ExecutePasses();
	CERT_API void SetResolution(uint32_t width, uint32_t height);
	CERT_API void GetResolution(uint32_t& width, uint32_t& height);
	CERT_API void WaitExecution();
	CERT_API void SetCopyTarget(ID3D12Resource* target);
	CERT_API void AttachModel(RE::TESForm* form);
	CERT_API void AttachLand(RE::TESForm* form, RE::NiAVObject* root);
	CERT_API void UpdateFeatureData(void* data, uint32_t size);
}