#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "RaytracingData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Types/RingBuffer.h"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"

namespace Pass::Raster
{
	// Water-only raster pass. The game omits transparent water from its
	// albedo/normal/depth targets, so GI has no primary hit there. This pass
	// rasters water meshes into owned RenderTargetManager targets (normals +
	// hardware depth) before GI, which fills those holes via depth test.
	class WaterNormals : public RenderPass
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

		// Fixed-function depth for self-occlusion between water meshes.
		// The shader-readable depth copy lives in RenderTargetManager.
		nvrhi::TextureHandle m_DepthTexture;

		// SceneTLAS raytracing buffer (carries WaterObjectUV / displacement
		// data); must run before this pass in the graph.
		SceneTLAS* m_SceneTLAS;

		void CheckGraphicsBindings();
		void CheckArgsBindings();

	public:
		void SceneTexturesChanged() override { m_GraphicsBindingSetDirty.fill(true); }
		WaterNormals(Renderer* renderer, SceneTLAS* sceneTLAS);

		virtual void Initialize() override;
		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
