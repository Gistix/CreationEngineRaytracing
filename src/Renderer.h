#pragma once

#include "Passes/RenderPass.h"
#include "Util.h"

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

struct Renderer
{
	spdlog::level::level_enum logLevel = spdlog::level::info;

	ID3D12Device5* m_NativeD3D12Device;
	ID3D11Device* m_NativeD3D11Device;

	nvrhi::DeviceHandle m_NVRHIDevice;

	nvrhi::CommandListHandle m_CommandList;

	uint64_t m_LastSubmittedInstance = 0;

	nvrhi::TextureHandle m_MainTexture;

	ID3D12Resource* m_CopyTargetResource = nullptr;
	nvrhi::TextureHandle m_CopyTargetTexture;

	uint2 m_RenderSize;
	uint2 m_PendingRenderSize;

	eastl::vector<eastl::unique_ptr<IRenderPass>> renderPasses;

	struct Settings
	{
		bool UseRayQuery = false;
		bool ValidationLayer = true;
	} settings;

	static Renderer* GetSingleton()
	{
		static Renderer singleton;
		return &singleton;
	}

	static auto GetDevice() { return GetSingleton()->m_NVRHIDevice; }

	static auto GetNativeD3D12Device() { return GetSingleton()->m_NativeD3D12Device; }

	nvrhi::ICommandList* GetCommandList() const
	{
		return m_CommandList;
	}

	bool Initialize(ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);

	void SetResolution(uint2 resolution);

	uint2 GetResolution();

	void CheckResolutionResources();

	void SetCopyTarget(ID3D12Resource* target);

	void ExecutePasses();

	void WaitExecution();

	void Load();

	void PostPostLoad();

	void DataLoaded();

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();
};