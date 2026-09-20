#pragma once

#include <PCH.h>
#include "Pass/RenderPass.h"
#include "interop/ASVGFData.hlsli"

namespace Pass::Denoiser
{
	class ASVGF : public RenderPass
	{
		Mode m_Mode;
		uint32_t m_HistoryIndex = 0;

		ASVGFSettings m_ASVGFSettings;

		nvrhi::SamplerHandle m_PointClampSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;

		nvrhi::BufferHandle m_ConstantBuffer;

		// Shaders & Pipelines
		nvrhi::ShaderHandle m_TemporalShader;
		nvrhi::ComputePipelineHandle m_TemporalPipeline;

		nvrhi::ShaderHandle m_GradientFilterShader;
		nvrhi::ComputePipelineHandle m_GradientFilterPipeline;

		nvrhi::ShaderHandle m_TemporalAccumShader;
		nvrhi::ComputePipelineHandle m_TemporalAccumPipeline;

		nvrhi::ShaderHandle m_VarianceEstimateShader;
		nvrhi::ComputePipelineHandle m_VarianceEstimatePipeline;

		nvrhi::ShaderHandle m_AtrousShader;
		nvrhi::ComputePipelineHandle m_AtrousPipeline;

		// Binding layouts
		nvrhi::BindingLayoutHandle m_TemporalBindingLayout;
		nvrhi::BindingLayoutHandle m_GradientFilterBindingLayout;
		nvrhi::BindingLayoutHandle m_TemporalAccumBindingLayout;
		nvrhi::BindingLayoutHandle m_VarianceEstimateBindingLayout;
		nvrhi::BindingLayoutHandle m_AtrousBindingLayout;

		enum SignalType { Diffuse = 0, Specular = 1, SignalCount = 2 };

		// Textures: separate history for diffuse and specular
		nvrhi::TextureHandle m_HistoryRadianceMoments[SignalCount][2];
		nvrhi::TextureHandle m_HistoryNormalDepth[SignalCount][2];
		nvrhi::TextureHandle m_HistoryLength[SignalCount][2];

		nvrhi::TextureHandle m_ReprojectedHistory;
		nvrhi::TextureHandle m_ReprojectedLength;
		nvrhi::TextureHandle m_TemporalGradient;
		nvrhi::TextureHandle m_FilteredGradient;

		nvrhi::TextureHandle m_RadiancePingPong[2];
		nvrhi::TextureHandle m_VariancePingPong[2];

		bool m_ResourcesDirty = true;

		// Cached binding sets per frame-in-flight slot
		nvrhi::BindingSetHandle m_TemporalBindingSets[Constants::MAX_FRAMES_IN_FLIGHT][SignalCount][2];
		nvrhi::BindingSetHandle m_GradientFilterBindingSets[Constants::MAX_FRAMES_IN_FLIGHT];
		nvrhi::BindingSetHandle m_TemporalAccumBindingSets[Constants::MAX_FRAMES_IN_FLIGHT][SignalCount][2];
		nvrhi::BindingSetHandle m_VarianceEstimateBindingSets[Constants::MAX_FRAMES_IN_FLIGHT][SignalCount][2];
		nvrhi::BindingSetHandle m_AtrousBindingSets[Constants::MAX_FRAMES_IN_FLIGHT][2];

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty { true, true };

		void CreateBindingLayouts();
		void CreatePipelines();
		void CreateResources();
		void CheckBindings(uint32_t slot);

		void DenoiseSignal(
			nvrhi::ICommandList* commandList,
			uint32_t slot,
			nvrhi::ITexture* outputRadiance,
			uint2 resolution,
			bool isSpecular);

	public:
		ASVGF(Renderer* renderer, Mode mode);
		~ASVGF() override = default;

		void Initialize() override;
		void SettingsChanged(const Settings& settings) override;
		void ResolutionChanged(uint2 resolution) override;
		void Execute(nvrhi::ICommandList* commandList) override;
		void ReloadShaders() override;
	};
}
