#pragma once

#include <PCH.h>
#include "Pass/RenderPass.h"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "GI12Resources.h"

namespace Pass::Raytracing::GI12
{
	class GI12WorldSpaceReSTIRPass : public RenderPass
	{
	private:
		GI12Resources* m_Resources = nullptr;
		SceneTLAS* m_SceneTLAS = nullptr;

		nvrhi::ShaderHandle m_ClearReservoirsShader;
		nvrhi::ComputePipelineHandle m_ClearReservoirsPipeline;

		nvrhi::ShaderHandle m_GenerateReservoirsShader;
		nvrhi::ComputePipelineHandle m_GenerateReservoirsPipeline;

		nvrhi::ShaderHandle m_GenerateMultibounceReservoirsShader;
		nvrhi::ComputePipelineHandle m_GenerateMultibounceReservoirsPipeline;

		nvrhi::ShaderHandle m_CompactReservoirsShader;
		nvrhi::ComputePipelineHandle m_CompactReservoirsPipeline;

		nvrhi::ShaderHandle m_ResampleReservoirsShader;
		nvrhi::ComputePipelineHandle m_ResampleReservoirsPipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty{};

		eastl::vector<ShaderDefine> m_Defines;

	public:
		GI12WorldSpaceReSTIRPass(Renderer* renderer, SceneTLAS* sceneTLAS, GI12Resources* resources);
		virtual ~GI12WorldSpaceReSTIRPass() = default;

		virtual void Initialize() override;
		virtual void CreatePipeline() override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void SettingsChanged(const Settings& settings) override;

		void CreateBindingLayout();
		void CheckBindings();
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
