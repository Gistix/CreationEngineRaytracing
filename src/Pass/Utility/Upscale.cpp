#include "Upscale.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"

namespace Pass::Utility
{
	Upscale::Upscale(Renderer* renderer, Pass::SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		m_LinearClampSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));
	}

	void Upscale::Initialize()
	{
		CreatePipeline();
	}

	void Upscale::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void Upscale::CreatePipeline()
	{
		CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();

		auto shaderBlob = ShaderCache::GetShader(L"data/shaders/Upscale.hlsl", {}, ShaderStage::Compute);
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

	void Upscale::SettingsChanged(const Settings& settings)
	{
		m_Enabled = (settings.RaytracingSettings.ResolutionScale != 1.0f);
	}

	void Upscale::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
	}

	void Upscale::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* scene = Scene::GetSingleton();
		auto* renderer = GetRenderer();
		auto& textureManager = renderer->RenderTargetManager();

		auto* diffuseRadiance = textureManager.GetTexture(RenderTarget::DiffuseRadiance);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, diffuseRadiance),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void Upscale::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		CheckBindings();

		auto* renderer = GetRenderer();

		auto* diffuseRadiance = renderer->RenderTargetManager().GetTexture(RenderTarget::DiffuseRadiance);

		auto scaledDynamicResolution = renderer->GetScaledDynamicResolution();
		auto copyRegion = nvrhi::TextureSlice{ 0, 0, 0, scaledDynamicResolution.x, scaledDynamicResolution.y, 1 };

		commandList->copyTexture(diffuseRadiance, copyRegion, renderer->GetMainTexture(), copyRegion);

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[renderer->GetCurrentSlot()] };
		commandList->setComputeState(state);

		auto threadGroupSize = Util::Math::GetDispatchCount(renderer->GetResolution(), 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}
