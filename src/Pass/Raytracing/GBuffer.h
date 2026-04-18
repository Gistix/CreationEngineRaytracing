#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/LightTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"

#include "Events/ITLASUpdateListener.h"

namespace Pass::Raytracing
{
	class GBuffer : public RenderPass, ITLASUpdateListener
	{
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::rt::PipelineHandle m_RayPipeline;
		nvrhi::rt::ShaderTableHandle m_ShaderTable;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::SamplerHandle m_LinearWrapSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;

		SceneTLAS* m_SceneTLAS;

		bool m_DirtyBindings = true;

		nvrhi::TextureHandle m_ViewDepth;

	public:
		GBuffer(Renderer* renderer, SceneTLAS* m_SceneTLAS);

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_DirtyBindings = true;
		}

		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		void CreateRootSignature();

		bool CreateRayTracingPipeline(eastl::vector<DxcDefine>& defines);

		bool CreateComputePipeline(eastl::vector<DxcDefine>& defines);

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}