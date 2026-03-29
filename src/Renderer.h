#pragma once

#include "Pass/RenderPass.h"
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

	nvrhi::CommandListHandle m_CommandList = nullptr;
	//nvrhi::CommandListHandle m_ComputeCommandList = nullptr;
	//nvrhi::CommandListHandle m_CopyCommandList = nullptr;

	uint64_t m_LastSubmittedInstance = 0;

	nvrhi::TextureHandle m_DepthTexture;
	nvrhi::TextureHandle m_MainTexture;

	ID3D12Resource* m_CopyTargetResource = nullptr;
	nvrhi::TextureHandle m_CopyTargetTexture;

	ID3D12Resource* m_PTDepthCopyTargetResource = nullptr;
	nvrhi::TextureHandle m_PTDepthCopyTargetTexture;
	ID3D12Resource* m_PTMVCopyTargetResource = nullptr;
	nvrhi::TextureHandle m_PTMVCopyTargetTexture;

	uint64_t m_FrameIndex = 0;

	uint2 m_RenderSize;
	uint2 m_PendingRenderSize;

	float2 m_DynamicResolutionRatio;

	float2 m_Jitter;

	eastl::unique_ptr<RenderGraph> m_RenderGraph;

	nvrhi::TimerQueryHandle m_FrameTimer;
	float m_FrameTime;

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
	void InitRR();

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

	// Diffuse albedo and normal roughness comes from RenderTargets (which are created externaly)
	struct RayReconstructionInput
	{
		nvrhi::TextureHandle diffuseAlbedo = nullptr;
		nvrhi::TextureHandle specularAlbedo = nullptr;
		nvrhi::TextureHandle specularHitDistance = nullptr;
	};

	eastl::unique_ptr<RayReconstructionInput> m_RayReconstructionInput;

	struct RenderTargets
	{
		nvrhi::TextureHandle albedo = nullptr;
		nvrhi::TextureHandle normalRoughness = nullptr;
#if defined(SKYRIM)
		nvrhi::TextureHandle gnmao = nullptr;
#endif
	};

	// Stable Planes resources for path decomposition
	struct StablePlanesResources
	{
		nvrhi::TextureHandle header = nullptr;          // R32_UINT, 2DArray, 4 slices: [0-2]=BranchIDs, [3]=firstHitRayLength|dominantIndex
		nvrhi::BufferHandle  buffer = nullptr;           // StructuredBuffer<StablePlane>, stride=80, count=3*W*H
		nvrhi::TextureHandle stableRadiance = nullptr;   // RGBA16_FLOAT, 2D: noise-free emissive along delta paths
	};
	eastl::unique_ptr<StablePlanesResources> m_StablePlanes;

	// PT Motion Vectors output (RGBA16_FLOAT, written by BUILD pass)
	nvrhi::TextureHandle m_PTMotionVectors;

	// PT Depth output (R32_FLOAT, clip-space depth)
	nvrhi::TextureHandle m_PTDepth;

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
		nvrhi::TextureHandle prevGBufferNormals = nullptr;   // RGBA16_FLOAT: previous frame normals
		bool needsNeighborOffsetUpload = false;
		eastl::vector<uint8_t> neighborOffsetData;
	};
	eastl::unique_ptr<ReSTIRGIResources> m_ReSTIRGIResources;

	eastl::unique_ptr<RenderTargets> m_RenderTargets;

	struct RendererSettings
	{
		bool UseRayQuery = true;
		bool ValidationLayer = true;
		bool VariableUpdateRate = false;
	} m_Settings;

	static Renderer* GetSingleton()
	{
		static Renderer singleton;
		return &singleton;
	}

	Renderer();

	auto GetDevice() const { return m_NVRHIDevice; }

	static auto GetNativeD3D12Device() { return GetSingleton()->m_NativeD3D12Device; }

	nvrhi::CommandListHandle GetComputeCommandList() const {
		return GetDevice()->createCommandList(
			nvrhi::CommandListParameters()
			.setQueueType(nvrhi::CommandQueue::Compute)
		);
	}

	nvrhi::CommandListHandle GetCopyCommandList() const {
		return GetDevice()->createCommandList(
			nvrhi::CommandListParameters()
			.setQueueType(nvrhi::CommandQueue::Copy)
		);
	}
	//nvrhi::ICommandList* GetCommandList() const { return m_CommandList; }
	
	RenderGraph* GetRenderGraph() { return m_RenderGraph.get(); }

	nvrhi::ITexture* GetDepthTexture();

	inline auto GetMainTexture() { return m_MainTexture; }

	inline auto GetFrameIndex() const { return m_FrameIndex; }

	inline auto GetJitter() const { return m_Jitter; }

	inline auto UpdateJitter(float2 jitter) { return m_Jitter = jitter; }

	inline auto& GetBlackTexture() const { return m_BlackTexture->texture; }

	inline auto& GetWhiteTextureIndex() const { return m_WhiteTexture->descriptorHandle; }
	inline auto& GetGrayTextureIndex() const { return m_GrayTexture->descriptorHandle; }
	inline auto& GetNormalTextureIndex() const { return m_NormalTexture->descriptorHandle; }
	inline auto& GetBlackTextureIndex() const { return m_BlackTexture->descriptorHandle; }
#if defined(SKYRIM)
	inline auto& GetRMAOSTextureIndex() const { return m_RMAOSTexture->descriptorHandle; }
#endif
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

	auto GetGBufferOutput() { 
		if (!m_GBufferOutput)
			InitGBufferOutput();

		return m_GBufferOutput.get();
	}

	auto GetRRInput() {
		if (!m_RayReconstructionInput)
			InitRR();

		return m_RayReconstructionInput.get();
	}

	auto GetRenderTargets() {
		if (!m_RenderTargets)
			m_RenderTargets = eastl::make_unique<RenderTargets>();

		return m_RenderTargets.get();
	}

	auto GetStablePlanes() {
		if (!m_StablePlanes)
			InitStablePlanes();

		return m_StablePlanes.get();
	}

	auto GetReSTIRGIResources() {
		if (!m_ReSTIRGIResources)
			InitReSTIRGI();

		return m_ReSTIRGIResources.get();
	}

	void InitStablePlanes();

	void InitReSTIRGI();

	void SetRenderTargets(ID3D12Resource* albedo, ID3D12Resource* normalRoughness, ID3D12Resource* gnmao);

	void SetDiffuseAlbedo(ID3D12Resource* diffuseAlbedo);

	nvrhi::TextureHandle CreateHandleForNativeTexture(ID3D12Resource* d3d11Texture, const char* debugName, nvrhi::Format format = nvrhi::Format::UNKNOWN, nvrhi::ResourceStates resourceState = nvrhi::ResourceStates::Unknown);

	nvrhi::TextureHandle ShareTexture(ID3D11Texture2D* d3d11Texture, const char* debugName, nvrhi::Format format, nvrhi::ResourceStates resourceState);

	void Initialize(RendererParams parameters);

	void InitDefaultTextures();

	void SetResolution(uint2 resolution);

	void SettingsChanged(const Settings& settings);

	uint2 GetResolution();

	uint2 GetDynamicResolution();

	void SetCopyTarget(ID3D12Resource* target);

	void SetPTOutputTargets(ID3D12Resource* depthTarget, ID3D12Resource* mvTarget);

	void ExecutePasses();

	void WaitExecution();

	void PostExecution();
};