#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Types/ShaderDefine.h"

namespace Pass::Common
{
	class PTComposite : public RenderPass
	{
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

	public:
		PTComposite(Renderer* renderer);

		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}