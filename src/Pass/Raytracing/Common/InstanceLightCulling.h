#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "ShaderUtils.h"

namespace Pass
{
	// Builds the per-instance light index list on the GPU. Runs after the scene TLAS so that the
	// instance buffer and light buffer are final for the frame, and before any lighting pass.
	class InstanceLightCulling : public RenderPass
	{
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

	public:
		InstanceLightCulling(Renderer* renderer);

		virtual void Initialize() override;
		void CreateBindingLayout();
		virtual void CreatePipeline() override;
		void CheckBindings();
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
