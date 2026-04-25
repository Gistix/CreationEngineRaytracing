#include "FaceNormals.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Utility
{
	FaceNormals::FaceNormals(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_LinearWrapSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		CreatePipeline();
	}

	void FaceNormals::CreatePipeline()
	{
		// Binding Layout
		{
			nvrhi::BindingLayoutDesc bindingLayoutDesc;
			bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
			bindingLayoutDesc.bindings = {
				nvrhi::BindingLayoutItem::Sampler(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_UAV(0)
			};

			m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(bindingLayoutDesc);
		}

		// Shader and Pipeline
		{
			auto device = GetRenderer()->GetDevice();

			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/FaceNormals.hlsl", {}, L"cs_6_5", L"Main");
			m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());

			auto pipelineDesc = nvrhi::ComputePipelineDesc()
				.setComputeShader(m_ComputeShader)
				.addBindingLayout(m_BindingLayout);

			m_ComputePipeline = device->createComputePipeline(pipelineDesc);
		}
	}

	void FaceNormals::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_DirtyBindings = true;
	}

	void FaceNormals::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto& textureManager = renderer->RenderTargetManager();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_UAV(0, textureManager.GetTexture(RenderTarget::FaceNormals))
		};

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}
	
	void FaceNormals::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSet };
		commandList->setComputeState(state);

		auto resolution = Renderer::GetSingleton()->GetDynamicResolution();
		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}