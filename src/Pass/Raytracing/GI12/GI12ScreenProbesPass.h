#pragma once

#include <PCH.h>
#include "Pass/RenderPass.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "GI12Resources.h"
#include "Events/ITLASUpdateListener.h"

namespace Pass::Raytracing::GI12
{
	class GI12ScreenProbesPass : public RenderPass, public ITLASUpdateListener
	{
	private:
		GI12Resources* m_Resources = nullptr;
		SceneTLAS* m_SceneTLAS = nullptr;

		// Compute Pipelines for Screen Probes stages
		nvrhi::ShaderHandle m_ClearProbeMaskShader;
		nvrhi::ComputePipelineHandle m_ClearProbeMaskPipeline;

		nvrhi::ShaderHandle m_InitCachedTileLRUShader;
		nvrhi::ComputePipelineHandle m_InitCachedTileLRUPipeline;

		nvrhi::ShaderHandle m_ReprojectScreenProbesShader;
		nvrhi::ComputePipelineHandle m_ReprojectScreenProbesPipeline;

		nvrhi::ShaderHandle m_CountAndScatterProbesShader;
		nvrhi::ComputePipelineHandle m_CountAndScatterProbesPipeline;

		nvrhi::ShaderHandle m_SpawnScreenProbesShader;
		nvrhi::ComputePipelineHandle m_SpawnScreenProbesPipeline;

		nvrhi::ShaderHandle m_SampleScreenProbesShader;
		nvrhi::ComputePipelineHandle m_SampleScreenProbesPipeline;

		nvrhi::ShaderHandle m_BlendScreenProbesShader;
		nvrhi::ComputePipelineHandle m_BlendScreenProbesPipeline;

		nvrhi::ShaderHandle m_ProjectScreenProbesShader;
		nvrhi::ComputePipelineHandle m_ProjectScreenProbesPipeline;

		nvrhi::ShaderHandle m_InterpolateScreenProbesShader;
		nvrhi::ComputePipelineHandle m_InterpolateScreenProbesPipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty{};

		eastl::vector<ShaderDefine> m_Defines;

	public:
		GI12ScreenProbesPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources);
		virtual ~GI12ScreenProbesPass() = default;

		virtual void Initialize() override;
		virtual void CreatePipeline() override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void SettingsChanged(const Settings& settings) override;
		virtual void SceneTexturesChanged() override { m_BindingSetDirty.fill(true); }
		virtual void OnTLASResized(TopLevelAS&) override { m_BindingSetDirty.fill(true); }

		void CreateBindingLayout();
		void CheckBindings();
		void ExecuteGenerate(nvrhi::ICommandList* commandList);
		void ExecuteInterpolate(nvrhi::ICommandList* commandList);
		virtual void Execute(nvrhi::ICommandList* commandList) override { ExecuteGenerate(commandList); }
	};

	class GI12ScreenProbesInterpolatePass : public RenderPass
	{
	private:
		GI12ScreenProbesPass* m_ParentPass = nullptr;

	public:
		GI12ScreenProbesInterpolatePass(Renderer* renderer, GI12ScreenProbesPass* parentPass)
			: RenderPass(renderer), m_ParentPass(parentPass) {}
		virtual ~GI12ScreenProbesInterpolatePass() = default;

		virtual void Execute(nvrhi::ICommandList* commandList) override
		{
			if (m_ParentPass) {
				m_ParentPass->ExecuteInterpolate(commandList);
			}
		}
	};
}
