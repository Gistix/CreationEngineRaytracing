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

	ID3D12Device5* device;
	ID3D11Device* d3d11Device;

	nvrhi::DeviceHandle m_NVRHIDevice;

	nvrhi::CommandListHandle m_CommandLists[2];
	uint32_t m_CommandListIndex = 0;

	uint64_t m_LastSubmittedInstance = 0;

	nvrhi::TextureHandle m_MainTexture;

	uint2 renderSize;
	uint2 pendingRenderSize;

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

	nvrhi::ICommandList* GetCommandList() const
	{
		return m_CommandLists[m_CommandListIndex];
	}

	bool Initialize(ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);

	void SetResolution(uint2 resolution);

	void CheckResolutionResources();

	void ExecutePasses();

	void WaitExecution();

	void Load();

	void PostPostLoad();

	void DataLoaded();

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();
};