#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"

namespace Pass::Common
{
	class Accumulation : public RenderPass
	{
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::TextureHandle m_AccumulationTexture;
		nvrhi::BufferHandle m_ConstantBuffer;

		bool m_DirtyBindings = true;

		uint32_t m_AccumulatedFrames = 0;

		// Camera state for change detection
		float4x4 m_PrevViewInverse = {};
		float4x4 m_PrevViewProj = {};  // unjittered
		float3   m_PrevPosition = {};

		// Settings hash for change detection
		size_t m_PrevSettingsHash = 0;

		bool DetectCameraChange() const;
		bool DetectSettingsChange(const Settings& settings);

	public:
		Accumulation(Renderer* renderer);

		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void CheckBindings();

		virtual void SettingsChanged(const Settings& settings) override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void Execute(nvrhi::ICommandList* commandList) override;

		uint32_t GetAccumulatedFrames() const { return m_AccumulatedFrames; }

		void ResetAccumulation() { m_AccumulatedFrames = 0; }
	};
}
