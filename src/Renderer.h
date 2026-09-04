#pragma once

#include "Pass/RenderPass.h"
#include "Util.h"
#include "CameraData.hlsli"

#include "Core/TextureManager.h"

#include "Types/PassTiming.h"

#include "Renderer/RenderGraph.h"
#include "Renderer/RenderTargetManager.h"

#include "Types/ShaderStage.h"

#include "Constants.h"

#include "Types/Settings.h"

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
	bool m_IsVulkan = false;

	ID3D12Device5* m_NativeD3D12Device;
	ID3D11Device5* m_NativeD3D11Device;

	nvrhi::DeviceHandle m_NVRHIDevice;

	nvrhi::CommandListHandle m_CommandList = nullptr;

	eastl::array<bool, magic_enum::enum_count<nvrhi::Feature>()> m_SupportedFeatures;

	// Fence used to synchronize 'executeCommandList' since it is not thread safe and we need the returned fence value to synchronize GPU resources
	mutable std::mutex m_ExecutionMutex;

	struct FrameSlot
	{
		nvrhi::CommandListHandle commandList = nullptr;
		nvrhi::EventQueryHandle eventQuery = nullptr;
		uint64_t fenceValue = 0;
		bool inFlight = false;
	};
	eastl::array<FrameSlot, Constants::MAX_FRAMES_IN_FLIGHT> m_FrameSlots;
	uint32_t m_CurrentSlot = 0;
	uint32_t m_NextSlot = 0;
	uint32_t m_LastCompletedSlot = 0;

	uint64_t m_LastSubmittedInstance = 0;

	// Original engine render targets (shared)
	nvrhi::TextureHandle m_DepthTexture;
	nvrhi::TextureHandle m_MotionVectorTexture;
	nvrhi::TextureHandle m_WaterDisplacementTexture;

	uint64_t m_FrameIndex = 0;

	uint2 m_RenderSize;
	uint2 m_PendingRenderSize;

	float2 m_DynamicResolutionRatio;

	float2 m_Jitter;
	float2 m_PrevJitter;

	eastl::unique_ptr<RenderGraph> m_RenderGraph;

	eastl::vector<PassTiming> m_PassTimings;

	eastl::array<nvrhi::TimerQueryHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_FrameTimerQueries = {};
	eastl::array<float, Constants::MAX_FRAMES_IN_FLIGHT> m_FrameCpuTimes = {};

	RenderTargetManager m_RenderTargetManager;

	eastl::unique_ptr<TextureReference> m_WhiteTexture;
	eastl::unique_ptr<TextureReference> m_GrayTexture;
	eastl::unique_ptr<TextureReference> m_NormalTexture;
	eastl::unique_ptr<TextureReference> m_BlackTexture;
#if defined(SKYRIM)
	eastl::unique_ptr<TextureReference> m_RMAOSTexture;
#endif
	eastl::unique_ptr<TextureReference> m_DetailTexture;

	inline static eastl::unordered_map<DXGI_FORMAT, nvrhi::Format> m_FormatMapping;

	void InitGBufferOutput();

	void PostInitialize();

public:
	struct GBufferOutput
	{
		nvrhi::TextureHandle depth = nullptr;
		nvrhi::TextureHandle motionVectors = nullptr;
		nvrhi::TextureHandle albedo = nullptr;
		nvrhi::TextureHandle normalRoughness = nullptr;
		nvrhi::TextureHandle emissiveMetallic = nullptr;
	};

	// GBuffer output from raster pass
	eastl::unique_ptr<GBufferOutput> m_GBufferOutput;

	struct RenderTargets
	{
		nvrhi::TextureHandle albedo = nullptr;
		nvrhi::TextureHandle normalRoughness = nullptr;
		nvrhi::TextureHandle gnmao = nullptr;
	};


	// ReSTIR GI resources
	struct ReSTIRGIResources
	{
		nvrhi::BufferHandle reservoirBuffer = nullptr;       // RWStructuredBuffer<RTXDI_PackedGIReservoir>, 2 arrays
		nvrhi::BufferHandle neighborOffsetBuffer = nullptr;  // Precomputed neighbor offsets for spatial resampling
		nvrhi::BufferHandle surfaceDataBuffer = nullptr;     // Packed primary surface data (ping-pong)
		nvrhi::TextureHandle secondaryGBufferPositionNormal = nullptr;  // RGBA32_FLOAT: position.xyz + packed normal
		nvrhi::TextureHandle secondaryGBufferRadiance = nullptr;        // RGBA32_FLOAT: radiance.xyz + pdf
		nvrhi::TextureHandle secondaryGBufferDiffuseAlbedo = nullptr;   // RGBA16_FLOAT: diffuse albedo
		nvrhi::TextureHandle secondaryGBufferSpecularF0Roughness = nullptr; // RGBA16_FLOAT: F0.xyz + roughness
		nvrhi::TextureHandle prevGBufferDepth = nullptr;     // R32_FLOAT: previous frame linear depth
		nvrhi::TextureHandle prevGBufferNormals = nullptr;   // RGBA16_SNORM: previous frame normals
		bool needsNeighborOffsetUpload = false;
		eastl::vector<uint8_t> neighborOffsetData;
	};
	eastl::unique_ptr<ReSTIRGIResources> m_ReSTIRGIResources;

	eastl::unique_ptr<RenderTargets> m_RenderTargets;

	D3D_SHADER_MODEL m_ShaderModel = D3D_SHADER_MODEL_6_5;

	RendererSettings m_Settings;

	static Renderer* GetSingleton()
	{
		static Renderer singleton;
		return &singleton;
	}

	Renderer();

	bool Initialize(RendererSettings* rendererSettings, ID3D11Device5* d3d11Device, ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue, ID3D12CommandQueue* computeCommandQueue, ID3D12CommandQueue* copyCommandQueue);

	bool Initialize(RendererSettings* rendererSettings, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, int graphicsQueueIndex, VkQueue transferQueue, int transferQueueIndex, VkQueue computeQueue, int computeQueueIndex);

	bool IsVulkan() const { return m_IsVulkan; }

	[[nodiscard]] const wchar_t* GetShaderStage(ShaderStage a_Stage) const noexcept;
	[[nodiscard]] std::wstring GetShaderTarget(ShaderStage a_Stage) const noexcept;

	nvrhi::IDevice* GetDevice() const { return m_NVRHIDevice; }

	auto& GetExecutionMutex() const { return m_ExecutionMutex; };

	static auto GetNativeD3D12Device() { return GetSingleton()->m_NativeD3D12Device; }
	static auto GetNativeD3D11Device() { return GetSingleton()->m_NativeD3D11Device; }

	nvrhi::CommandListHandle GetGraphicsCommandList() const {
		return GetDevice()->createCommandList(
			nvrhi::CommandListParameters()
			.setQueueType(nvrhi::CommandQueue::Graphics)
			.setEnableImmediateExecution(false)
			.setUploadChunkSize(Constants::NVRHI_CMDLIST_UPLOAD_CHUNK_SIZE)
			.setScratchChunkSize(Constants::NVRHI_CMDLIST_SCRATCH_CHUNK_SIZE)
		);
	}

	nvrhi::CommandListHandle GetComputeCommandList() const {
		return GetDevice()->createCommandList(
			nvrhi::CommandListParameters()
			.setQueueType(nvrhi::CommandQueue::Compute)
			.setEnableImmediateExecution(false)
			.setUploadChunkSize(Constants::NVRHI_CMDLIST_UPLOAD_CHUNK_SIZE)
			.setScratchChunkSize(Constants::NVRHI_CMDLIST_SCRATCH_CHUNK_SIZE)
		);
	}

	nvrhi::CommandListHandle GetCopyCommandList() const {
		return GetDevice()->createCommandList(
			nvrhi::CommandListParameters()
			.setQueueType(nvrhi::CommandQueue::Copy)
			.setEnableImmediateExecution(false)
			.setUploadChunkSize(Constants::NVRHI_CMDLIST_UPLOAD_CHUNK_SIZE)
			.setScratchChunkSize(Constants::NVRHI_CMDLIST_SCRATCH_CHUNK_SIZE)
		);
	}

	RenderGraph* GetRenderGraph() { return m_RenderGraph.get(); }

	nvrhi::ITexture* GetDepthTexture();
	nvrhi::ITexture* GetMotionVectorTexture();
	nvrhi::ITexture* GetWaterDisplacementTexture();

	inline auto GetLastSubmittedFence() const { return m_LastSubmittedInstance; }

	inline auto GetMainTexture() { return m_RenderTargetManager.GetTexture(RenderTarget::Main, m_CurrentSlot); }

	inline auto GetFrameIndex() const { return m_FrameIndex; }

	inline auto GetCurrentSlot() const { return m_CurrentSlot; }
	inline auto GetCompletedSlot() const { return m_LastCompletedSlot; }

	inline auto& GetFrameTimerQuery(uint32_t slot) { return m_FrameTimerQueries[slot]; }
	inline void SetFrameCpuTime(uint32_t slot, float ms) { m_FrameCpuTimes[slot] = ms; }
	inline float GetFrameCpuTime(uint32_t slot) const { return m_FrameCpuTimes[slot]; }

	inline auto GetJitter() const { return m_Jitter; }

	inline auto GetPrevJitter() const { return m_PrevJitter; }

	inline void UpdateJitter(float2 jitter) { 
		m_PrevJitter = m_Jitter;
		m_Jitter = jitter;
	}

	inline auto& RenderTargetManager() { return m_RenderTargetManager; }

	inline auto& GetBlackDescriptor() const { return m_BlackTexture->texture; }

	inline auto& GetWhiteTextureDescriptor() const { return m_WhiteTexture->descriptorHandle; }
	inline auto& GetGrayTextureDescriptor() const { return m_GrayTexture->descriptorHandle; }
	inline auto& GetNormalTextureDescriptor() const { return m_NormalTexture->descriptorHandle; }
	inline nvrhi::ITexture* GetNormalTexture() const { return m_NormalTexture->texture; }
	inline auto& GetBlackTextureDescriptor() const { return m_BlackTexture->descriptorHandle; }
#if defined(SKYRIM)
	inline auto& GetRMAOSTextureDescriptor() const { return m_RMAOSTexture->descriptorHandle; }
#endif
	inline auto& GetDetailTextureDescriptor() const { return m_DetailTexture->descriptorHandle; }

	inline auto GetPassTimings() const { return m_PassTimings; };

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

	auto GetGBufferOutput() { 
		if (!m_GBufferOutput)
			InitGBufferOutput();

		return m_GBufferOutput.get();
	}

	auto GetRenderTargets() {
		if (!m_RenderTargets)
			m_RenderTargets = eastl::make_unique<RenderTargets>();

		return m_RenderTargets.get();
	}

	auto GetReSTIRGIResources() {
		if (!m_ReSTIRGIResources)
			InitReSTIRGI();

		return m_ReSTIRGIResources.get();
	}

	void InitReSTIRGI();

	void SetRenderTargets(ID3D12Resource* albedo, ID3D12Resource* normalRoughness, ID3D12Resource* gnmao);

	nvrhi::TextureHandle CreateHandleForNativeTexture(ID3D12Resource* d3d11Texture, const char* debugName, nvrhi::Format format = nvrhi::Format::UNKNOWN, nvrhi::ResourceStates resourceState = nvrhi::ResourceStates::Unknown);

	nvrhi::TextureHandle ShareTexture(ID3D11Texture2D* d3d11Texture, const char* debugName, nvrhi::Format format = nvrhi::Format::UNKNOWN, nvrhi::ResourceStates resourceState = nvrhi::ResourceStates::Unknown);

	void InitDefaultTextures();

	void SetResolution(uint2 resolution);

	void SettingsChanged(const Settings& settings);

	uint2 GetResolution();

	uint2 GetDynamicResolution();

	uint2 GetScaledDynamicResolution();

	nvrhi::ICommandList* StartExecution();

	void EndExecution();

	uint32_t PostExecution();

	void RunPostExecutionForSlot(uint32_t slot);
};
