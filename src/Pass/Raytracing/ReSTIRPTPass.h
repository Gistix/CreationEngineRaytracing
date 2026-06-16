#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"

#include <Rtxdi/PT/ReSTIRPT.h>

namespace Pass::Raytracing
{
	class ReSTIRPTPass : public RenderPass
	{
		// RTXDI PT Context (manages buffer indices, frame tracking)
		eastl::unique_ptr<rtxdi::ReSTIRPTContext> m_Context;

		// Constant buffer for RTXDI PT parameters
		nvrhi::BufferHandle m_ConstantBuffer;

		// Compute pipelines for each resampling stage
		nvrhi::ShaderHandle m_TemporalShader;
		nvrhi::ComputePipelineHandle m_TemporalPipeline;

		nvrhi::ShaderHandle m_SpatialShader;
		nvrhi::ComputePipelineHandle m_SpatialPipeline;

		nvrhi::ShaderHandle m_FinalShadingShader;
		nvrhi::ComputePipelineHandle m_FinalShadingPipeline;

		nvrhi::ShaderHandle m_InitialSamplingShader;
		nvrhi::ComputePipelineHandle m_InitialSamplingPipeline;

		// Binding layout and sets
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointWrapSampler;

		SceneTLAS* m_SceneTLAS;

		bool m_DirtyBindings = true;
		bool m_Enabled = false;
		rtxdi::ReSTIRPT_ResamplingMode m_ResamplingMode = rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial;
		eastl::vector<ShaderDefine> m_Defines;

	public:
		ReSTIRPTPass(Renderer* renderer, SceneTLAS* sceneTLAS);

		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;

		void CreateBindingLayout();

		void CheckBindings();

		void FillConstantBuffer(nvrhi::ICommandList* commandList);

		void DispatchInitialSampling(nvrhi::ICommandList* commandList, const nvrhi::BindingSetVector& bindings, uint2 threadGroupSize);

		void CopyCurrentGBufferToPrevious(nvrhi::ICommandList* commandList);
	};
}
