#pragma once

#include "core/Shape.h"
#include "core/Model.h"
#include "FrameData.hlsli"

#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"


struct MessageCallback : public nvrhi::IMessageCallback
{
	static MessageCallback& GetInstance()
	{
		static MessageCallback instance;
		return instance;
	}

	void message(nvrhi::MessageSeverity severity, const char* messageText) override
	{
		switch (severity) {
		case nvrhi::MessageSeverity::Fatal:
			logger::critical("{}", messageText);
			break;
		case nvrhi::MessageSeverity::Error:
			logger::error("{}", messageText);
			break;
		case nvrhi::MessageSeverity::Warning:
			logger::warn("{}", messageText);
			break;
		case nvrhi::MessageSeverity::Info:
			logger::info("{}", messageText);
			break;
		}
	}
};

struct Scene
{
	ID3D12Device5* device;
	ID3D11Device* d3d11Device;

	nvrhi::DeviceHandle m_NVRHIDevice;

	nvrhi::ShaderLibraryHandle m_ShaderLibrary;
	nvrhi::rt::PipelineHandle m_RayPipeline;
	nvrhi::rt::ShaderTableHandle m_ShaderTable;
	nvrhi::ShaderHandle m_ComputeShader;
	nvrhi::ComputePipelineHandle m_ComputePipeline;
	nvrhi::CommandListHandle m_CommandList;
	
	nvrhi::BindingLayoutHandle m_BindingLayout;
	nvrhi::BindingSetHandle m_BindingSet;
	nvrhi::BindingLayoutHandle m_BindlessLayout;

	nvrhi::rt::AccelStructHandle m_TopLevelAS;

	nvrhi::BufferHandle m_ConstantBuffer;

	std::shared_ptr<DescriptorTableManager> m_DescriptorTable;
	nvrhi::TextureHandle m_ColorBuffer;

	eastl::unique_ptr<FrameData> frameData;

	eastl::unordered_map<eastl::string, eastl::unique_ptr<Model*>> models;

	struct Settings
	{
		bool UseRayQuery = false;
		bool ValidationLayer = true;
	} settings;

	static Scene* GetSingleton()
	{
		static Scene singleton;
		return &singleton;
	}

	bool Initialize(ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue);

	void UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const;

	void CreateRootSignature();

	bool CreateRayTracingPipeline();

	bool CreateComputePipeline();

	void InitializeLog(spdlog::level::level_enum a_level);

	void AttachModel(RE::TESForm* form);

	void AttachLand(RE::TESForm* form, RE::NiAVObject* root);
};