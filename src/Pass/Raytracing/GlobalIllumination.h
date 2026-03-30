#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"
#include "CameraData.hlsli"
#include "ShaderUtils.h"
#include "framework/DescriptorTableManager.h"
#include "Util.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Pass/Raytracing/Common/SHaRC.h"

#include "Events/ITLASUpdateListener.h"

namespace Pass::Raytracing
{
	class GlobalIllumination : public RenderPass, ITLASUpdateListener
	{
		nvrhi::ShaderLibraryHandle m_ShaderLibrary;
		nvrhi::rt::PipelineHandle m_RayPipeline;
		nvrhi::rt::ShaderTableHandle m_ShaderTable;
		nvrhi::ShaderHandle m_ComputeShader;
		nvrhi::ComputePipelineHandle m_ComputePipeline;

		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;

		nvrhi::SamplerHandle m_LinearWrapSampler;

		SceneTLAS* m_SceneTLAS;
		SHaRC* m_SHaRC;

		bool m_DirtyBindings = true;
		nvrhi::TextureHandle m_LastDiffuseTexture;
		nvrhi::TextureHandle m_LastSpecularTexture;
		nvrhi::TextureHandle m_LastSpecularHitDistTexture;

		eastl::vector<ShaderDefine> m_Defines;

	public:
		GlobalIllumination(Renderer* renderer, SceneTLAS* sceneTLAS, SHaRC* sharc);

		void OnTLASResized([[maybe_unused]] TopLevelAS& tlas) override
		{
			m_DirtyBindings = true;
		}

		virtual void CreatePipeline() override;

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		void CreateBindingLayout();

		void CreateRayTracingPipeline();

		void CreateComputePipeline();

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}
