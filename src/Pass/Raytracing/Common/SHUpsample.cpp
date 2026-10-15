#include "SHUpsample.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "Util.h"

namespace Pass::Common
{
	SHUpsample::SHUpsample(Renderer* renderer, Pass::SceneTLAS* sceneTLAS, SHResolveTemporal* shResolve)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_SHResolve(shResolve)
	{
		m_LinearClampSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		m_PointClampSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(false));
	}

	void SHUpsample::Initialize()
	{
		CreatePipeline();
	}

	void SHUpsample::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc bindingLayoutDesc;
		bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		bindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // Feature
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // Raytracing
			nvrhi::BindingLayoutItem::Texture_SRV(0), // DiffuseSHHistory (array)
			nvrhi::BindingLayoutItem::Texture_SRV(1), // SpecularSHHistory (array)
			nvrhi::BindingLayoutItem::Texture_SRV(2), // ViewDepth (low-res)
			nvrhi::BindingLayoutItem::Texture_SRV(3), // FullResDepth
			nvrhi::BindingLayoutItem::Texture_SRV(4), // Albedo
			nvrhi::BindingLayoutItem::Texture_SRV(5), // FullResNormalRoughness
			nvrhi::BindingLayoutItem::Texture_SRV(6), // DownscaledMotionVectors (debug)
			nvrhi::BindingLayoutItem::Texture_UAV(0) // Output (main)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(bindingLayoutDesc);
	}

	void SHUpsample::CreatePipeline()
	{
		CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();

		eastl::vector<DxcDefine> defines;
		if (kDebugMotionVectors)
			defines.emplace_back(DxcDefine{ L"SH_DEBUG_MV", L"1" });

		auto shaderBlob = ShaderCache::GetShader(L"data/shaders/SHUpsample.hlsl", defines, ShaderStage::Compute);
		if (!shaderBlob)
			return;

		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void SHUpsample::SettingsChanged(const Settings& settings)
	{
		m_Enabled = (settings.GeneralSettings.Denoiser == Denoiser::SH_Temporal);
	}

	void SHUpsample::ResolutionChanged(uint2)
	{
		m_BindingSetDirty.fill(true);
	}

	void SHUpsample::CheckBindings()
	{
		auto* scene = Scene::GetSingleton();
		auto* renderer = GetRenderer();
		auto* renderTargets = renderer->GetRenderTargets();
		auto& textureManager = renderer->RenderTargetManager();

		uint32_t currentSlot = renderer->GetCurrentSlot();

		const uint32_t historyVersion = m_SHResolve->GetHistoryVersion();
		if (historyVersion != m_BoundHistoryVersion[currentSlot])
			m_BindingSetDirty[currentSlot] = true;

		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(1, m_PointClampSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, m_SHResolve->GetDiffuseHistory()),
			nvrhi::BindingSetItem::Texture_SRV(1, m_SHResolve->GetSpecularHistory()),
			nvrhi::BindingSetItem::Texture_SRV(2, textureManager.GetTexture(RenderTarget::ViewDepth)),
			nvrhi::BindingSetItem::Texture_SRV(3, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(4, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(5, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(6, textureManager.GetTexture(RenderTarget::DownscaledMotionVectors)),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BoundHistoryVersion[currentSlot] = historyVersion;
		m_BindingSetDirty[currentSlot] = false;
	}

	void SHUpsample::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		if (!m_ComputePipeline || !m_BindingSets[currentSlot])
			return;

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		const auto resolution = GetRenderer()->GetDynamicResolution();
		const auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}
