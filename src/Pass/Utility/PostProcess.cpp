#include "PostProcess.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Utility
{
	PostProcess::PostProcess(Renderer* renderer, Mode mode, Pass::SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_Mode(mode), m_SceneTLAS(sceneTLAS)
	{
		m_PointClampSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(false));
	}

	void PostProcess::Initialize()
	{
		m_Denoiser = Scene::GetSingleton()->m_Settings.GeneralSettings.Denoiser;
		CreatePipeline();
	}

	void PostProcess::SettingsChanged(const Settings& settings)
	{
		m_Enabled = (
			settings.GeneralSettings.Mode == Mode::GlobalIllumination && 
			(settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
			 settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax ||
			 settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR ||
			 settings.RaytracingSettings.ResolutionScale != 1.0f)
		);

		if (settings.GeneralSettings.Denoiser != m_Denoiser) {
			m_Denoiser = settings.GeneralSettings.Denoiser;
			CreatePipeline();
			m_BindingSetDirty.fill(true);
		}
	}

	void PostProcess::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::BindingLayoutDesc bindingLayoutDesc;
		bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		bindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),                // PointClampSampler
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // Raytracing
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // Features
			nvrhi::BindingLayoutItem::Texture_SRV(0),            // DepthTexture
			nvrhi::BindingLayoutItem::Texture_SRV(1),            // NormalRoughnessTexture
			nvrhi::BindingLayoutItem::Texture_SRV(2),            // PrimaryMotionVectors
			nvrhi::BindingLayoutItem::Texture_UAV(0),            // OutNormalRoughness
			nvrhi::BindingLayoutItem::Texture_UAV(1)             // OutMotionVectors
		};

		eastl::vector<DxcDefine> defines;

		if (m_Denoiser == Denoiser::NRD_Reblur || m_Denoiser == Denoiser::NRD_Relax) {
			bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2)); // OutViewDepth
			defines.push_back({ L"NRD", L"1" });
		}

		if (m_Denoiser == Denoiser::DLSS_RR) {
			bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(3)); // AlbedoTexture
			bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(4)); // GNMAOTexture
			bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2)); // OutSpecularAlbedo
			defines.push_back({ L"DLSS_RR", L"1" });
		}

		m_BindingLayout = device->createBindingLayout(bindingLayoutDesc);

		winrt::com_ptr<IDxcBlob> blob;
		ShaderUtils::CompileShader(blob, L"data/shaders/PostProcess.hlsl", defines, L"cs_6_5", L"main");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "main" }, blob->GetBufferPointer(), blob->GetBufferSize());

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void PostProcess::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
	}

	void PostProcess::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto& textureManager = renderer->RenderTargetManager();
		auto* renderTargets = renderer->GetRenderTargets();

		nvrhi::ITexture* sourceDepth = nullptr;
		nvrhi::ITexture* sourceMotionVectors = nullptr;

		if (m_Mode == Mode::GlobalIllumination) {
			sourceDepth = renderer->GetDepthTexture();
			sourceMotionVectors = renderer->GetMotionVectorTexture();
		} else {
			sourceDepth = textureManager.GetTexture(RenderTarget::ClipDepth);
			sourceMotionVectors = textureManager.GetTexture(RenderTarget::MotionVectors3D);
		}

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_PointClampSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, sourceDepth),
			nvrhi::BindingSetItem::Texture_SRV(1, renderTargets ? renderTargets->normalRoughness.Get() : nullptr),
			nvrhi::BindingSetItem::Texture_SRV(2, sourceMotionVectors),
			nvrhi::BindingSetItem::Texture_UAV(0, textureManager.GetTexture(RenderTarget::DownscaledNormalRoughness)),
			nvrhi::BindingSetItem::Texture_UAV(1, textureManager.GetTexture(RenderTarget::DownscaledMotionVectors))
		};

		if (m_Denoiser == Denoiser::NRD_Reblur || m_Denoiser == Denoiser::NRD_Relax) {
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(2, textureManager.GetTexture(RenderTarget::ViewDepth)));
		}

		if (m_Denoiser == Denoiser::DLSS_RR) {
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, renderTargets->albedo));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(4, renderTargets->gnmao));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(2, textureManager.GetTexture(RenderTarget::RRSpecularAlbedo)));
		}

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void PostProcess::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		auto resolution = GetRenderer()->GetScaledDynamicResolution();
		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}
