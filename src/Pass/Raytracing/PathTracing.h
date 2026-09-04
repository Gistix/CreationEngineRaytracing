#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
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
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::rt::PipelineHandle m_RayPipeline;
		nvrhi::rt::ShaderTableHandle m_ShaderTable;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;

		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointWrapSampler;

		SceneTLAS* m_SceneTLAS;

		SHaRC* m_SHaRC;

		eastl::vector<ShaderDefine> m_Defines;

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};
		bool m_UseStablePlanes = false;  // Toggle for stable planes vs reference mode
		bool m_UseRestirGI = false;
	public:
		PathTracing(Renderer* renderer, SceneTLAS* m_SceneTLAS, SHaRC* sharc);

		virtual void Initialize() override;

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_BindingSetDirty.fill(true);
		}

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		void CreateBindingLayout();

		virtual void CreatePipeline() override;
		void CreateRayTracingPipeline();
		void CreateComputePipeline();

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;

		void ExecuteDispatch(nvrhi::ICommandList* commandList, nvrhi::rt::PipelineHandle rayPipeline, nvrhi::rt::ShaderTableHandle shaderTable, nvrhi::ComputePipelineHandle computePipeline);
	};
}