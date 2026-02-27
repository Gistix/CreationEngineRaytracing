#include "GIComposite.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	GIComposite::GIComposite(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_LinearWrapSampler = GetRenderer()->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		CreatePipeline();
	}

	void GIComposite::CreatePipeline()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::Texture_SRV(3),
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};
		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);

		eastl::vector<DxcDefine> defines = {};

		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/GIComposite.hlsl", defines, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = GetRenderer()->GetDevice()->createComputePipeline(pipelineDesc);
	}

	void GIComposite::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_DirtyBindings = true;
	}

	void GIComposite::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();

		auto* renderGraph = renderer->GetRenderGraph();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, Scene::GetSingleton()->GetCameraBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, renderGraph->GetTexture("DirectInput")),
			nvrhi::BindingSetItem::Texture_SRV(1, renderGraph->GetTexture("AlbedoInput")),
			nvrhi::BindingSetItem::Texture_SRV(2, renderGraph->GetTexture("DiffuseIndirectOutput")),
			nvrhi::BindingSetItem::Texture_SRV(3, renderGraph->GetTexture("SpecularIndirectOutput")),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void GIComposite::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		auto resolution = Renderer::GetSingleton()->GetResolution();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSet };
		commandList->setComputeState(state);

		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 16);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}