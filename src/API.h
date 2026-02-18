#pragma once

extern "C" {
	CERT_API void Initialize(ID3D12Device5* device, ID3D12CommandQueue* commandQueue);
	CERT_API void UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position);
	CERT_API void AttachModel(RE::TESForm* form);
	CERT_API void AttachLand(RE::TESForm* form, RE::NiAVObject* root);
}