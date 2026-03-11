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

#include "Types/ShaderDefine.h"

namespace Pass
{
	class PathTracing : public RenderPass
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
		LightTLAS* m_LightTLAS;
		SHaRC* m_SHaRC;

		bool m_DirtyBindings = true;

		struct RayReconstructionInputs
		{
			nvrhi::TextureHandle diffuseAlbedoTexture;
			nvrhi::TextureHandle specularAlbedoTexture;
			nvrhi::TextureHandle normalRoughnessTexture;
			nvrhi::TextureHandle specularHitDistTexture;
		};

		eastl::vector<ShaderDefine> m_Defines;

	public:
		PathTracing(Renderer* renderer, SceneTLAS* m_SceneTLAS, LightTLAS* lightTLAS, SHaRC* sharc);

		virtual void ResolutionChanged(uint2 resolution) override;

		virtual void SettingsChanged(const Settings& settings) override;

		void CreateBindingLayout();

		virtual void CreatePipeline() override;

		void CreateRayTracingPipeline();

		void CreateComputePipeline();

		void CheckBindings();

		virtual void Execute(nvrhi::ICommandList* commandList) override;
	};
}