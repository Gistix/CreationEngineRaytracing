#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "ShaderUtils.h"

namespace Pass::Utility
{
	class PostProcess : public RenderPass
	{
		Mode m_Mode;
		Pass::SceneTLAS* m_SceneTLAS = nullptr;
		nvrhi::SamplerHandle m_PointClampSampler;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

	public:
		PostProcess(Renderer* renderer, Mode mode, Pass::SceneTLAS* sceneTLAS);

		virtual void Initialize() override;
		void CreatePipeline();
		void CheckBindings();

		virtual void SettingsChanged(const Settings& settings) override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
