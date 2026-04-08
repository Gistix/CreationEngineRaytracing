#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"

#include "Types/ShaderDefine.h"

#include "Events/ITLASUpdateListener.h"

namespace Pass
{
	class PathTracing : public RenderPass, ITLASUpdateListener
	{
		// Reference mode (original single-pass) pipeline
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::rt::PipelineHandle m_RayPipeline;
		nvrhi::rt::ShaderTableHandle m_ShaderTable;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		// Stable Planes BUILD pass pipeline
		nvrhi::ShaderHandle m_BuildComputeShader;
		nvrhi::ComputePipelineHandle m_BuildComputePipeline;
		nvrhi::rt::PipelineHandle m_BuildRayPipeline;
		nvrhi::rt::ShaderTableHandle m_BuildShaderTable;

		// Stable Planes FILL pass pipeline
		nvrhi::ShaderHandle m_FillComputeShader;
		nvrhi::ComputePipelineHandle m_FillComputePipeline;
		nvrhi::rt::PipelineHandle m_FillRayPipeline;
		nvrhi::rt::ShaderTableHandle m_FillShaderTable;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::SamplerHandle m_LinearWrapSampler;

		SceneTLAS* m_SceneTLAS;

		SHaRC* m_SHaRC;

		eastl::vector<ShaderDefine> m_Defines;

		bool m_DirtyBindings = true;
		bool m_UseStablePlanes = false;  // Toggle for stable planes vs reference mode
		bool m_UseRestirGI = false;
	public:
		PathTracing(Renderer* renderer, SceneTLAS* m_SceneTLAS, SHaRC* sharc);

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_DirtyBindings = true;
		}

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void CreateRayTracingPipeline();
		void CreateRayTracingPipelineForMode(int mode, nvrhi::rt::PipelineHandle& outPipeline, nvrhi::rt::ShaderTableHandle& outShaderTable);

		void CreateComputePipeline();
		void CreateComputePipelineForMode(int mode, nvrhi::ShaderHandle& outShader, nvrhi::ComputePipelineHandle& outPipeline);

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;

		void ExecuteDispatch(nvrhi::ICommandList* commandList, nvrhi::rt::PipelineHandle rayPipeline, nvrhi::rt::ShaderTableHandle shaderTable, nvrhi::ComputePipelineHandle computePipeline);
	};
}