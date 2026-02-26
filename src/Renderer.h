#pragma once

#include "Passes/RenderPass.h"
#include "Util.h"
#include "CameraData.hlsli"
#include "Types/RendererParams.h"
#include "Types/TextureReference.h"

#include "Renderer/RenderGraph.h"

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
	ID3D11Device5* m_NativeD3D11Device;

	nvrhi::DeviceHandle m_NVRHIDevice;

	nvrhi::CommandListHandle m_CommandList;

	uint64_t m_LastSubmittedInstance = 0;

	nvrhi::TextureHandle m_MainTexture;

	ID3D12Resource* m_CopyTargetResource = nullptr;
	nvrhi::TextureHandle m_CopyTargetTexture;

	uint64_t m_FrameIndex = 0;

	uint2 m_RenderSize;
	uint2 m_PendingRenderSize;

	eastl::unique_ptr<RenderGraph> m_RenderGraph;

	nvrhi::TimerQueryHandle m_FrameTimer;
	float m_FrameTime;

	eastl::unique_ptr<TextureReference> m_WhiteTexture;
	eastl::unique_ptr<TextureReference> m_GrayTexture;
	eastl::unique_ptr<TextureReference> m_NormalTexture;
	eastl::unique_ptr<TextureReference> m_BlackTexture;
#if defined(SKYRIM)
	eastl::unique_ptr<TextureReference> m_RMAOSTexture;
	eastl::unique_ptr<TextureReference> m_DetailTexture;
#endif

	inline static eastl::unordered_map<DXGI_FORMAT, nvrhi::Format> m_FormatMapping;

	spdlog::level::level_enum logLevel = spdlog::level::info;

public:

	struct Settings
	{
		bool UseRayQuery = true;
		bool ValidationLayer = true;
		bool VariableUpdateRate = false;
	} settings;

	static Renderer* GetSingleton()
	{
		static Renderer singleton;
		return &singleton;
	}

	Renderer();

	auto GetDevice() { return m_NVRHIDevice; }

	static auto GetNativeD3D12Device() { return GetSingleton()->m_NativeD3D12Device; }

	nvrhi::ICommandList* GetCommandList() const { return m_CommandList; }

	RenderGraph* GetRenderGraph() { return m_RenderGraph.get(); }

	inline auto GetMainTexture() { return m_MainTexture; }

	inline auto GetFrameIndex() const { return m_FrameIndex; }

	inline auto& GetWhiteTextureIndex() const { return m_WhiteTexture->descriptorHandle; }
	inline auto& GetGrayTextureIndex() const { return m_GrayTexture->descriptorHandle; }
	inline auto& GetNormalTextureIndex() const { return m_NormalTexture->descriptorHandle; }
	inline auto& GetBlackTextureIndex() const { return m_BlackTexture->descriptorHandle; }
	inline auto& GetRMAOSTextureIndex() const { return m_RMAOSTexture->descriptorHandle; }
	inline auto& GetDetailTextureIndex() const { return m_DetailTexture->descriptorHandle; }

	static inline auto& GetFormatMapping() { return m_FormatMapping; }
	
	inline float* GetFrameTime() { return &m_FrameTime; };

	static inline auto GetFormat(DXGI_FORMAT nativeFormat) 
	{ 
		auto it = m_FormatMapping.find(nativeFormat);

		if (it == m_FormatMapping.end()) {
			return nvrhi::Format::UNKNOWN;
		}

		return it->second;
	}

	static uint GetUpdateInterval(float distance)
	{
		float t = std::log2((distance - 25.0f) + 1.0f) * 0.3f;
		return std::clamp(static_cast<uint>(t), 0u, 30u);
	}

	uint2 GetRenderSize() const { return m_RenderSize; }

	void Load();

	void PostPostLoad();

	void DataLoaded();

	void SetLogLevel(spdlog::level::level_enum a_level = spdlog::level::info);
	spdlog::level::level_enum GetLogLevel();

	void Initialize(RendererParams parameters);

	void InitDefaultTextures();

	void InitRenderPasses();

	void SetResolution(uint2 resolution);

	uint2 GetResolution();

	void CheckResolutionResources();

	void SetCopyTarget(ID3D12Resource* target);

	void ExecutePasses();

	void WaitExecution();
};