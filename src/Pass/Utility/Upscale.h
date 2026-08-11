#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"

namespace Pass::Utility
{
	class Upscale : public RenderPass
	{
		Pass::SceneTLAS* m_SceneTLAS = nullptr;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

	public:
		Upscale(Renderer* renderer, Pass::SceneTLAS* sceneTLAS);

		virtual void Initialize() override;
		void CreateBindingLayout();
		virtual void CreatePipeline() override;
		void CheckBindings();

		virtual void SettingsChanged(const Settings& settings) override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
