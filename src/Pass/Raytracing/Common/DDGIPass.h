#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "interop/CameraData.hlsli"
#include "interop/RaytracingData.hlsli"
#include "interop/DDGIData.hlsli"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Events/ITLASUpdateListener.h"

namespace Pass::Raytracing::Common
{
	class DDGIPass : public RenderPass, public ITLASUpdateListener
	{
		// Constant & Structured Buffers
		struct DDGIRootConstants
		{
			uint32_t VolumeIndex = 0;
			uint32_t VolumeConstantsIndex = 0;
			uint32_t VolumeResourceIndicesIndex = 0;
			uint32_t ReductionInputSizeX = 0;
			uint32_t ReductionInputSizeY = 0;
			uint32_t ReductionInputSizeZ = 0;
			uint32_t Padding[2] = {};
		};
		static_assert(sizeof(DDGIRootConstants) == 32);
		DDGIRootConstants m_DDGIRootConstants;
		eastl::unique_ptr<DDGIData> m_DDGIData;
		nvrhi::BufferHandle m_ConstantBuffer;
		nvrhi::BufferHandle m_VolumeDescBuffer;

		// Textures
		nvrhi::TextureHandle m_ProbeIrradiance;
		nvrhi::TextureHandle m_ProbeVariability;
		nvrhi::TextureHandle m_ProbeDistance;
		nvrhi::TextureHandle m_ProbeData;
		nvrhi::TextureHandle m_RayData;

		// Subpasses
		struct SubPass
		{
			bool m_Initialized = false;
			nvrhi::ShaderHandle m_ComputeShader;
			nvrhi::ComputePipelineHandle m_ComputePipeline;
			nvrhi::rt::PipelineHandle m_RayPipeline;
			nvrhi::rt::ShaderTableHandle m_ShaderTable;
			nvrhi::BindingLayoutHandle m_BindingLayout;
			eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		};

		SubPass m_TracePass;
		SubPass m_BlendIrradiancePass;
		SubPass m_BlendDistancePass;
		SubPass m_RelocationPass;
		SubPass m_ClassificationPass;
		SubPass m_ResolvePass;

		// Samplers
		nvrhi::SamplerHandle m_DefaultSampler;
		nvrhi::SamplerHandle m_ClampSampler;
		nvrhi::SamplerHandle m_PointWrapSampler;
		nvrhi::SamplerHandle m_BilinearClampSampler;

		SceneTLAS* m_SceneTLAS;

		// State & Scrolling
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty{};
		bool m_ResetCache = true;
		uint64_t m_LightingRevision = UINT64_MAX;
		uint32_t m_FrameCounter = 0;

		float3 m_AnchorPosition = { 0.0f, 0.0f, 0.0f };
		struct Int3 { int x = 0; int y = 0; int z = 0; };
		Int3 m_ScrollOffsets = { 0, 0, 0 };
		Int3 m_ProbeCounts = { 16, 16, 12 };
		float3 m_ProbeSpacing = { 140.0f, 140.0f, 140.0f };
		int m_RaysPerProbe = 144;

		void CreateTextures();
		void SetupTracePass();
		void SetupBlendPasses();
		void SetupRelocationPass();
		void SetupClassificationPass();
		void SetupResolvePass();
		void CheckBindings();
		void ClearCache(nvrhi::ICommandList* commandList);
		void UpdateVolumeDesc();

	public:
		DDGIPass(Renderer* renderer, SceneTLAS* sceneTLAS);

		virtual void Initialize() override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void SettingsChanged(const Settings& settings) override;
		virtual void SceneTexturesChanged() override { m_BindingSetDirty.fill(true); }

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_BindingSetDirty.fill(true);
		}

		virtual void Execute(nvrhi::ICommandList* commandList) override;

		auto GetProbeIrradiance() const { return m_ProbeIrradiance; }
		auto GetProbeDistance() const { return m_ProbeDistance; }
		auto GetProbeData() const { return m_ProbeData; }
		auto GetRayData() const { return m_RayData; }
		auto GetVolumeConstantBuffer() const { return m_ConstantBuffer; }
		uint32_t GetProbeCount() const { return static_cast<uint32_t>(m_ProbeCounts.x * m_ProbeCounts.y * m_ProbeCounts.z); }
	};
}
