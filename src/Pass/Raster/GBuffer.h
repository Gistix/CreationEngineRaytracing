#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Types/RingBuffer.h"
#include "Util.h"

namespace Pass::Raster
{
	class GBuffer : public RenderPass
	{
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::ShaderHandle m_ArgsShader;

		nvrhi::GraphicsPipelineHandle m_GraphicsPipeline;
		nvrhi::ComputePipelineHandle m_ArgsPipeline;

		RingBuffer m_IndirectArgsBuffer;

		nvrhi::BindingLayoutHandle m_GraphicsBindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_GraphicsBindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_GraphicsBindingSetDirty{};

		nvrhi::BindingLayoutHandle m_ArgsBindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_ArgsBindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_ArgsBindingSetDirty{};

		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointWrapSampler;

		nvrhi::FramebufferHandle m_FrameBuffer;

		eastl::unique_ptr<RaytracingData> m_RaytracingData;
		nvrhi::BufferHandle m_RaytracingBuffer;

		void CheckGraphicsBindings();
		void CheckArgsBindings();

	public:
		GBuffer(Renderer* renderer);

		virtual void Initialize() override;
		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}