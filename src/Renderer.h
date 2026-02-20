#pragma once

#include "Passes/RenderPass.h"
#include "Util.h"
#include "CameraData.hlsli"

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

class Renderer
{
	ID3D12Device5* m_NativeD3D12Device;
	ID3D11Device* m_NativeD3D11Device;

	nvrhi::DeviceHandle m_NVRHIDevice;

	nvrhi::CommandListHandle m_CommandList;

	uint64_t m_LastSubmittedInstance = 0;

	nvrhi::TextureHandle m_MainTexture;

	ID3D12Resource* m_CopyTargetResource = nullptr;
	nvrhi::TextureHandle m_CopyTargetTexture;

	uint64_t m_FrameIndex = 0;

	uint2 m_RenderSize;
	uint2 m_PendingRenderSize;

	eastl::unique_ptr<CameraData> m_CameraData;
	nvrhi::BufferHandle m_CameraDataBuffer;

	eastl::vector<eastl::unique_ptr<RenderPass>> m_RenderPasses;

	spdlog::level::level_enum logLevel = spdlog::level::info;

public:

	struct Settings
	{
		bool UseRayQuery = false;
		bool ValidationLayer = true;
		bool VariableUpdateRate = false;
	} settings;

	static Renderer* GetSingleton()
	{
		static Renderer singleton;
		return &singleton;
	}

	auto GetDevice() { return m_NVRHIDevice; }

	static auto GetNativeD3D12Device() { return GetSingleton()->m_NativeD3D12Device; }

	nvrhi::ICommandList* GetCommandList() const { return m_CommandList; }

	inline auto GetCameraDataBuffer() const { return m_CameraDataBuffer; }

	inline auto GetMainTexture() { return m_MainTexture; }

	inline auto GetFrameIndex() const { return m_FrameIndex; }

	inline auto GetCameraData() const { return m_CameraData.get(); }

	static uint GetUpdateInterval(float distance)
	{
		float t = std::log2((distance - 25.0f) + 1.0f) * 0.3f;
		return std::clamp(static_cast<uint>(t), 0u, 30u);
	}

	void Load();

	void PostPostLoad();

	void DataLoaded();

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();


	bool Initialize(ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);

	void SetResolution(uint2 resolution);

	uint2 GetResolution();

	void CheckResolutionResources();

	void UpdateCameraData(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const;

	void SetCopyTarget(ID3D12Resource* target);

	void ExecutePasses();

	void WaitExecution();
};