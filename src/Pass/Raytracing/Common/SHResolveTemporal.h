#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "Pass/Raytracing/Common/SceneTLAS.h"

namespace Pass::Common
{
	// Fused SH project + temporal accumulate pass (low-res GI grid).
	// Reads per-pixel demodulated radiance + first-bounce incident direction
	// from GlobalIllumination, projects the single sample into L2 SH in
	// registers, reprojects the SH history with downscaled motion vectors,
	// and writes the accumulated SH back to the ping-pong history.
	class SHResolveTemporal : public RenderPass
	{
		static constexpr uint32_t SH_SLICES = 7;
		static constexpr uint32_t MAX_ACCUMULATED_FRAMES = 16;

		Pass::SceneTLAS* m_SceneTLAS = nullptr;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointClampSampler;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<eastl::array<nvrhi::BindingSetHandle, 2>, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

		// Ping-pong SH history: [lobe 0 = diffuse, 1 = specular][read/write slot].
		// Each entry is a 7-slice RGBA16F array (see shaders/include/SH.hlsli).
		eastl::array<eastl::array<nvrhi::TextureHandle, 2>, 2> m_SHHistory;
		eastl::array<nvrhi::TextureHandle, 2> m_HistoryDepth;
		eastl::array<nvrhi::TextureHandle, 2> m_HistoryNormal;
		uint2 m_HistoryResolution { 0, 0 };

		uint32_t m_WriteIndex = 0;
		uint32_t m_AccumulatedFrames = 0;
		uint64_t m_LightingRevision = UINT64_MAX;
		uint32_t m_HistoryVersion = 0;

		void CreateHistory(uint2 resolution);
		void CreateBindingLayout();
		void CheckBindings();

	public:
		SHResolveTemporal(Renderer* renderer, Pass::SceneTLAS* sceneTLAS);

		virtual void Initialize() override;
		virtual void CreatePipeline() override;

		virtual void SettingsChanged(const Settings& settings) override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void Execute(nvrhi::ICommandList* commandList) override;

		nvrhi::ITexture* GetDiffuseHistory() const { return m_SHHistory[0][m_WriteIndex ^ 1].Get(); }
		nvrhi::ITexture* GetSpecularHistory() const { return m_SHHistory[1][m_WriteIndex ^ 1].Get(); }
		uint32_t GetHistoryVersion() const { return m_HistoryVersion; }
	};
}
