#pragma once

#include <PCH.h>
#include "Pass/RenderPass.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "GI12Resources.h"

namespace Pass::Raytracing::GI12
{
	class GI12DenoiserPass : public RenderPass
	{
	private:
		GI12Resources* m_Resources = nullptr;
		SceneTLAS* m_SceneTLAS = nullptr;

		nvrhi::ShaderHandle m_ReprojectGIShader;
		nvrhi::ComputePipelineHandle m_ReprojectGIPipeline;

		nvrhi::ShaderHandle m_FilterGIShader;
		nvrhi::ComputePipelineHandle m_FilterGIPipeline;

		nvrhi::ShaderHandle m_CompositeGIShader;
		nvrhi::ComputePipelineHandle m_CompositeGIPipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty{};

		eastl::vector<ShaderDefine> m_Defines;

	public:
		GI12DenoiserPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources);
		virtual ~GI12DenoiserPass() = default;

		virtual void Initialize() override;
		virtual void CreatePipeline() override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void SettingsChanged(const Settings& settings) override;
		virtual void SceneTexturesChanged() override { m_BindingSetDirty.fill(true); }

		void CreateBindingLayout();
		void CheckBindings();
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
