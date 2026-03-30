#pragma once

#include <PCH.h>

#include "Pass/RenderPass.h"

#include <NRD.h>

namespace Pass::NRD
{
	class ReblurRadiance : public RenderPass
	{
		struct Pipeline
		{
			nvrhi::ShaderHandle shader;
			nvrhi::ComputePipelineHandle pipeline;
			eastl::string debugName;
		};

		static constexpr nrd::Denoiser kDenoiser = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;
		static constexpr nrd::Identifier kDenoiserIdentifier = nrd::Identifier(kDenoiser);

		nvrhi::SamplerHandle m_NearestClampSampler;
		nvrhi::SamplerHandle m_LinearClampSampler;

		nvrhi::BindingLayoutHandle m_GlobalBindingLayout;
		nvrhi::BindingLayoutHandle m_ResourceBindingLayout;
		nvrhi::BindingSetHandle m_GlobalBindingSet;
		nvrhi::BufferHandle m_ConstantBuffer;

		eastl::vector<Pipeline> m_Pipelines;
		eastl::vector<nvrhi::TextureHandle> m_PermanentPool;
		eastl::vector<nvrhi::TextureHandle> m_TransientPool;

		nvrhi::TextureHandle m_MotionVectorsScratch;
		nvrhi::TextureHandle m_FallbackSrvTexture;
		nvrhi::TextureHandle m_FallbackUavTexture;

		nrd::Instance* m_NRD = nullptr;
		nrd::ReblurSettings m_ReblurSettings = {};

		nrd::CommonSettings m_CommonSettings = {};

		bool m_ResourcesDirty = true;
		bool m_SettingsDirty = true;

		void Setup();
		void DestroyInstance();
		void CreateBindingLayouts();
		void CreatePipelines();
		void CreateResources();
		void CreateGlobalBindingSet();

		void UpdateCommonSettings();

		nvrhi::ITexture* GetDispatchResource(const nrd::ResourceDesc& resource) const;
		nvrhi::Format GetFormat(nrd::Format format) const;

		uint32_t GetMaxResourceCount(nrd::DescriptorType type) const;

	public:
		ReblurRadiance(Renderer* renderer);
		~ReblurRadiance() override;

		void SettingsChanged(const Settings& settings) override;
		void ResolutionChanged(uint2 resolution) override;
		void Execute(nvrhi::ICommandList* commandList) override;
	};
}
