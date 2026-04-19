#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

namespace Pass::Raster
{
	class GBuffer : public RenderPass
	{
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;

		nvrhi::GraphicsPipelineHandle m_GraphicsPipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;

		nvrhi::FramebufferHandle m_FrameBuffer;

		eastl::unique_ptr<RaytracingData> m_RaytracingData;
		nvrhi::BufferHandle m_RaytracingBuffer;

	public:
		GBuffer(Renderer* renderer);

		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}