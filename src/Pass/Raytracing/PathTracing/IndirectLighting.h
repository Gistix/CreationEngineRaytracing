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
	class IndirectLighting : public RenderPass, ITLASUpdateListener
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

		eastl::vector<ShaderDefine> m_Defines;

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

	public:
		IndirectLighting(Renderer* renderer, SceneTLAS* sceneTLAS);

		virtual void Initialize() override;

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_BindingSetDirty.fill(true);
		}

		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		void CreateBindingLayout();

		bool CreateRayTracingPipeline();

		bool CreateComputePipeline();

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
