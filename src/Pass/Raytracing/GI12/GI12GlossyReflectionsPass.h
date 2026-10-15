#pragma once

#include <PCH.h>
#include "Pass/RenderPass.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "GI12Resources.h"
#include "Events/ITLASUpdateListener.h"

namespace Pass::Raytracing::GI12
{
	class GI12GlossyReflectionsPass : public RenderPass, public ITLASUpdateListener
	{
	private:
		GI12Resources* m_Resources = nullptr;
		SceneTLAS* m_SceneTLAS = nullptr;

		nvrhi::ShaderHandle m_ClearCountersShader;
		nvrhi::ComputePipelineHandle m_ClearCountersPipeline;

		nvrhi::ShaderHandle m_TraceReflectionsShader;
		nvrhi::ComputePipelineHandle m_TraceReflectionsPipeline;

		nvrhi::ShaderHandle m_ResolveReflectionsAtrousFirstShader;
		nvrhi::ComputePipelineHandle m_ResolveReflectionsAtrousFirstPipeline;

		nvrhi::ShaderHandle m_ResolveReflectionsAtrousIterShader;
		nvrhi::ComputePipelineHandle m_ResolveReflectionsAtrousIterPipeline;

		nvrhi::ShaderHandle m_ResolveReflectionsAtrousLastShader;
		nvrhi::ComputePipelineHandle m_ResolveReflectionsAtrousLastPipeline;

		nvrhi::ShaderHandle m_MarkFirefliesShader;
		nvrhi::ComputePipelineHandle m_MarkFirefliesPipeline;

		nvrhi::ShaderHandle m_CleanupFirefliesShader;
		nvrhi::ComputePipelineHandle m_CleanupFirefliesPipeline;

		nvrhi::ShaderHandle m_ReprojectReflectionsShader;
		nvrhi::ComputePipelineHandle m_ReprojectReflectionsPipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty{};

		eastl::vector<ShaderDefine> m_Defines;

	public:
		GI12GlossyReflectionsPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources);
		virtual ~GI12GlossyReflectionsPass() = default;

		virtual void Initialize() override;
		virtual void CreatePipeline() override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void SettingsChanged(const Settings& settings) override;
		virtual void SceneTexturesChanged() override { m_BindingSetDirty.fill(true); }
		virtual void OnTLASResized(TopLevelAS&) override { m_BindingSetDirty.fill(true); }

		void CreateBindingLayout();
		void CheckBindings();
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
