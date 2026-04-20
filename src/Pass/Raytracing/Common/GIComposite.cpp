#include "GIComposite.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Common
{
	GIComposite::GIComposite(Renderer* renderer)
		: RenderPass(renderer)
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GIComposite::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::Texture_SRV(3),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void GIComposite::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/GIComposite.hlsl", { { L"NRD_REBLUR", L"1" } }, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void GIComposite::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* scene = Scene::GetSingleton();

		auto* renderer = GetRenderer();

		auto* renderTargets = renderer->GetRenderTargets();

		auto& textureManager = renderer->RenderTargetManager();

		auto* diffuseTexture = textureManager.GetTexture(RenderTarget::DiffuseRadiance);
		auto* specularTexture = textureManager.GetTexture(RenderTarget::SpecularRadiance);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(1, renderTargets->gnmao),
			nvrhi::BindingSetItem::Texture_SRV(2, diffuseTexture),
			nvrhi::BindingSetItem::Texture_SRV(3, specularTexture),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

		m_BindingSet = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

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

		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}