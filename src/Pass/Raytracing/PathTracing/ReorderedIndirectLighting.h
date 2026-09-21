#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Events/ITLASUpdateListener.h"
#include "Types/ShaderDefine.h"

namespace Pass::Raytracing::PathTracing
{
	class ReorderedIndirectLighting : public RenderPass, ITLASUpdateListener
	{
	public:
		static constexpr uint32_t kNumBins = 4096;

	private:
		// 1. Generate Bounce
		nvrhi::ShaderHandle m_GenerateShader;
		nvrhi::ComputePipelineHandle m_GeneratePipeline;
		nvrhi::BindingLayoutHandle m_GenerateBindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_GenerateBindingSets;

		// 2. Prefix Sum Bins
		nvrhi::ShaderHandle m_PrefixSumShader;
		nvrhi::ComputePipelineHandle m_PrefixSumPipeline;
		nvrhi::BindingLayoutHandle m_PrefixSumBindingLayout;
		nvrhi::BindingSetHandle m_PrefixSumBindingSet;

		// 3. Scatter Rays
		nvrhi::ShaderHandle m_ScatterShader;
		nvrhi::ComputePipelineHandle m_ScatterPipeline;
		nvrhi::BindingLayoutHandle m_ScatterBindingLayout;
		nvrhi::BindingSetHandle m_ScatterBindingSet;

		// 4. Trace Bounce
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::rt::PipelineHandle m_RayPipeline;
		nvrhi::rt::ShaderTableHandle m_ShaderTable;
		nvrhi::ShaderHandle m_ComputeTraceShader;
		nvrhi::ComputePipelineHandle m_ComputeTracePipeline;
		nvrhi::BindingLayoutHandle m_TraceBindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_TraceBindingSets;

		// Buffers
		nvrhi::BufferHandle m_RayRecordBuffer;
		nvrhi::BufferHandle m_RayKeyBuffer;
		nvrhi::BufferHandle m_SortedIndexBuffer;
		nvrhi::BufferHandle m_CounterBuffer;
		nvrhi::BufferHandle m_BinHistogramBuffer;
		nvrhi::BufferHandle m_BinOffsetBuffer;
		nvrhi::BufferHandle m_BinCounterBuffer;

		// Samplers
		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointWrapSampler;

		SceneTLAS* m_SceneTLAS;

		eastl::vector<ShaderDefine> m_Defines;

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_GenerateBindingSetDirty {};
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_TraceBindingSetDirty {};
		bool m_ReorderBindingSetsDirty = true;
		uint2 m_AllocatedResolution = { 0, 0 };

	public:
		ReorderedIndirectLighting(Renderer* renderer, SceneTLAS* sceneTLAS);

		virtual void Initialize() override;

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_TraceBindingSetDirty.fill(true);
		}

		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		void CreateBindingLayouts();

		bool CreateGeneratePipeline();
		bool CreateReorderPipelines();
		bool CreateTracePipeline();

		void AllocateBuffers(uint2 resolution);

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
