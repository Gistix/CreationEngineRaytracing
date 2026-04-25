#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

namespace Pass::Utility
{
	class FaceNormals : public RenderPass
	{
		nvrhi::ShaderHandle m_ComputeShader;

		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::SamplerHandle m_LinearWrapSampler;

		bool m_DirtyBindings = true;

	public:
		FaceNormals(Renderer* renderer);

		virtual void CreatePipeline() override;

		virtual void CheckBindings();

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}