#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"

namespace Pass::Raytracing::Common
{
	class DDGIPass;

	class DDGIDebugPass : public RenderPass
	{
		DDGIPass* m_DDGIPass;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::ComputePipelineHandle m_ComputePipeline;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty{};
		nvrhi::TextureHandle m_ProbeGridTexture;
		nvrhi::FramebufferHandle m_ProbeGridFramebuffer;
		nvrhi::BindingLayoutHandle m_ProbeGridBindingLayout;
		nvrhi::GraphicsPipelineHandle m_ProbeGridPipeline;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_ProbeGridBindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_ProbeGridBindingSetDirty{};
		bool m_LoggedProbeGridExecution = false;

		void CheckBindings();
		void CreateProbeGridTarget(uint2 resolution);
		void CheckProbeGridBindings();
		void ExecuteProbeGrid(nvrhi::ICommandList* commandList);

	public:
		DDGIDebugPass(Renderer* renderer, DDGIPass* ddgiPass);

		void Initialize() override;
		void SettingsChanged(const Settings& settings) override;
		void ResolutionChanged(uint2 resolution) override;
		void SceneTexturesChanged() override { m_BindingSetDirty.fill(true); m_ProbeGridBindingSetDirty.fill(true); }
		void Execute(nvrhi::ICommandList* commandList) override;
	};
}
