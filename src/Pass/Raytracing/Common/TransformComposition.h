#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "ShaderUtils.h"

namespace Pass
{
	class TransformComposition : public RenderPass
	{
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

	public:
		TransformComposition(Renderer* renderer);

		void CreateBindingLayout();
		virtual void CreatePipeline() override;
		void CheckBindings();
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
