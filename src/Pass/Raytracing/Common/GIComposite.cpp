#include "GIComposite.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Common
{
	GIComposite::GIComposite(Renderer* renderer, Pass::SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
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

	void GIComposite::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GIComposite::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::Texture_SRV(3),
			nvrhi::BindingLayoutItem::Texture_SRV(4),
			nvrhi::BindingLayoutItem::Texture_SRV(5),
			nvrhi::BindingLayoutItem::Texture_SRV(6),
			nvrhi::BindingLayoutItem::Texture_UAV(0)		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void GIComposite::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/GIComposite.hlsl", {}, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void GIComposite::SettingsChanged(const Settings& settings)
	{
		m_Enabled = (settings.GeneralSettings.Denoiser == Denoiser::NRD);
	}

	void GIComposite::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* scene = Scene::GetSingleton();
		auto* renderer = GetRenderer();
		auto* renderTargets = renderer->GetRenderTargets();
		auto& textureManager = renderer->RenderTargetManager();

		auto* diffuseTexture = textureManager.GetTexture(RenderTarget::DiffuseRadiance);
		auto* specularTexture = textureManager.GetTexture(RenderTarget::SpecularRadiance);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(1, m_PointClampSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, diffuseTexture),
			nvrhi::BindingSetItem::Texture_SRV(1, specularTexture),
			nvrhi::BindingSetItem::Texture_SRV(2, textureManager.GetTexture(RenderTarget::ViewDepth)),
			nvrhi::BindingSetItem::Texture_SRV(3, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(4, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(5, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(6, renderTargets->gnmao),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void GIComposite::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto resolution = GetRenderer()->GetResolution();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}