#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/SHResolveTemporal.h"

namespace Pass::Common
{
	// Full-res SH decode + joint-bilateral upscale pass.
	// Reads the temporally accumulated low-res L2 SH history, upsamples the
	// coefficients with depth-gated bilateral weights, evaluates them with
	// the full-res shading normal, and remodulates with full-res albedo.
	class SHUpsample : public RenderPass
	{
		// Diagnostic: visualize downscaled motion vectors instead of GI.
		// Flip back to false after confirming live vectors.
		static constexpr bool kDebugMotionVectors = false;

		Pass::SceneTLAS* m_SceneTLAS = nullptr;
		SHResolveTemporal* m_SHResolve = nullptr;

		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointClampSampler;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		eastl::array<nvrhi::BindingSetHandle, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSets;
		eastl::array<bool, Constants::MAX_FRAMES_IN_FLIGHT> m_BindingSetDirty {};
		eastl::array<uint32_t, Constants::MAX_FRAMES_IN_FLIGHT> m_BoundHistoryVersion { UINT32_MAX, UINT32_MAX, UINT32_MAX };

		void CreateBindingLayout();
		void CheckBindings();

	public:
		SHUpsample(Renderer* renderer, Pass::SceneTLAS* sceneTLAS, SHResolveTemporal* shResolve);

		virtual void Initialize() override;
		virtual void CreatePipeline() override;

		virtual void SettingsChanged(const Settings& settings) override;
		virtual void ResolutionChanged(uint2 resolution) override;
		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
