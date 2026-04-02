#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "ShaderUtils.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"

#include <Rtxdi/GI/ReSTIRGI.h>

namespace Pass::Raytracing
{
	class ReSTIRGIPass : public RenderPass
	{
		// RTXDI Context (manages buffer indices, frame tracking)
		eastl::unique_ptr<rtxdi::ReSTIRGIContext> m_Context;

		// Constant buffer for RTXDI GI parameters
		nvrhi::BufferHandle m_ConstantBuffer;

		// Compute pipelines for each resampling stage
		nvrhi::ShaderHandle m_TemporalShader;
		nvrhi::ComputePipelineHandle m_TemporalPipeline;

		nvrhi::ShaderHandle m_SpatialShader;
		nvrhi::ComputePipelineHandle m_SpatialPipeline;

		nvrhi::ShaderHandle m_FusedShader;
		nvrhi::ComputePipelineHandle m_FusedPipeline;

		nvrhi::ShaderHandle m_FinalShadingShader;
		nvrhi::ComputePipelineHandle m_FinalShadingPipeline;

		// Binding layout and sets
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::SamplerHandle m_LinearWrapSampler;

		SceneTLAS* m_SceneTLAS;

		bool m_DirtyBindings = true;
		bool m_Enabled = false;
		rtxdi::ReSTIRGI_ResamplingMode m_ResamplingMode = rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial;
		eastl::vector<ShaderDefine> m_Defines;

	public:
		ReSTIRGIPass(Renderer* renderer, SceneTLAS* sceneTLAS);

		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;

		void CreateBindingLayout();

		void CheckBindings();

		void FillConstantBuffer(nvrhi::ICommandList* commandList);

		void CopyCurrentGBufferToPrevious(nvrhi::ICommandList* commandList);
	};
}
