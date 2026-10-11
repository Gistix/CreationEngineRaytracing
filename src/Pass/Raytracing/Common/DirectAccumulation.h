#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"

namespace Pass::Common
{
	// Temporally accumulates the separated primary-surface direct lighting
	// (see SPLIT_DIRECT) with motion-vector reprojection. Direct is nearly
	// noise-free, so a cheap reprojected exponential blend preserves sharp
	// shadow/highlight detail that NRD would overblur, while the noisy
	// indirect signal keeps going through NRD.
	class DirectAccumulation : public RenderPass
	{
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;
		nvrhi::SamplerHandle m_LinearClampSampler;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;

		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};

		uint64_t m_LightingRevision = UINT64_MAX;
		bool m_ResetHistory = true;

		bool IsActive(const Settings& settings) const;

	public:
		DirectAccumulation(Renderer* renderer);

		virtual void Initialize() override;
		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void CheckBindings();

		virtual void SettingsChanged(const Settings& settings) override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
