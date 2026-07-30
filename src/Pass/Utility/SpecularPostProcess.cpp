#include "SpecularPostProcess.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Utility
{
	SpecularPostProcess::SpecularPostProcess(Renderer* renderer)
		: RenderPass(renderer)
	{
		CreatePipeline();
	}

	void SpecularPostProcess::SettingsChanged(const Settings& settings)
	{
		m_Enabled = (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR);
	}

	void SpecularPostProcess::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		// Binding Layout
		{
			nvrhi::BindingLayoutDesc bindingLayoutDesc;
			bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
			bindingLayoutDesc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
				nvrhi::BindingLayoutItem::Texture_SRV(0), // ClipDepth
				nvrhi::BindingLayoutItem::Texture_SRV(1), // NormalRoughness
				nvrhi::BindingLayoutItem::Texture_SRV(2), // MotionVectors3D
				nvrhi::BindingLayoutItem::Texture_SRV(3), // RRSpecularHitDist
				nvrhi::BindingLayoutItem::Texture_UAV(0)  // OutSpecularMotionVectors / RRSpecularMotionVectors
			};

			m_BindingLayout = device->createBindingLayout(bindingLayoutDesc);
		}

		// Shader and Pipeline
		{
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/SpecularPostProcess.hlsl", {}, L"cs_6_5", L"main");
			m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "main" }, blob->GetBufferPointer(), blob->GetBufferSize());

			auto pipelineDesc = nvrhi::ComputePipelineDesc()
				.setComputeShader(m_ComputeShader)
				.addBindingLayout(m_BindingLayout);

			m_ComputePipeline = device->createComputePipeline(pipelineDesc);
		}
	}

	void SpecularPostProcess::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
	}

	void SpecularPostProcess::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto& textureManager = renderer->RenderTargetManager();

		auto* renderTargets = renderer->GetRenderTargets();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(0, textureManager.GetTexture(RenderTarget::ClipDepth)),
			nvrhi::BindingSetItem::Texture_SRV(1, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(2, textureManager.GetTexture(RenderTarget::MotionVectors3D)),
			nvrhi::BindingSetItem::Texture_SRV(3, textureManager.GetTexture(RenderTarget::RRSpecularHitDist)),
			nvrhi::BindingSetItem::Texture_UAV(0, textureManager.GetTexture(RenderTarget::RRSpecularMotionVectors))
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void SpecularPostProcess::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		auto resolution = Renderer::GetSingleton()->GetDynamicResolution();
		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}
