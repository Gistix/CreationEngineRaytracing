#include "PTComposite.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Common
{
	PTComposite::PTComposite(Renderer* renderer)
		: RenderPass(renderer)
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void PTComposite::CreateBindingLayout()
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
			nvrhi::BindingLayoutItem::Texture_SRV(4),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void PTComposite::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/PTComposite.hlsl", { { L"NRD", L"1" }, { L"NRD_REBLUR", L"1" }}, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void PTComposite::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* scene = Scene::GetSingleton();

		auto* renderer = GetRenderer();

		auto& textureManager = renderer->RenderTargetManager();

		auto* diffuseAlbedo = textureManager.GetTexture(RenderTarget::DiffuseAlbedo);

		auto* diffuseRadiance = textureManager.GetTexture(RenderTarget::DiffuseRadiance);
		auto* specularRadiance = textureManager.GetTexture(RenderTarget::SpecularRadiance);

		auto* diffuseFactor = textureManager.GetTexture(RenderTarget::DiffuseFactor);
		auto* specularFactor = textureManager.GetTexture(RenderTarget::SpecularFactor);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, scene->GetFeatureBuffer()),			
			nvrhi::BindingSetItem::Texture_SRV(0, diffuseAlbedo),
			nvrhi::BindingSetItem::Texture_SRV(1, diffuseRadiance),
			nvrhi::BindingSetItem::Texture_SRV(2, specularRadiance),
			nvrhi::BindingSetItem::Texture_SRV(3, diffuseFactor),
			nvrhi::BindingSetItem::Texture_SRV(4, specularFactor),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

		m_BindingSet = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void PTComposite::Execute(nvrhi::ICommandList* commandList)
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