#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Interop/VertexUpdate.hlsli"
#include "Interop/BoneMatrix.hlsli"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"

#include "Types/ShaderDefine.h"

namespace Pass::Common
{
	class GIComposite : public RenderPass
	{
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		bool m_DirtyBindings = true;

	public:
		GIComposite(Renderer* renderer);

		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}