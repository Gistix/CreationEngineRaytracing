#include "DirectAccumulation.h"
#include "Renderer.h"
#include "Scene.h"
#include "Util.h"

namespace Pass::Common
{
	struct DirectAccumulationPushConstants
	{
		uint32_t resetHistory = 1;
		float blendFactor = 0.2f;
		float depthThreshold = 0.05f;
		uint32_t _pad = 0;
	};

	DirectAccumulation::DirectAccumulation(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_LinearClampSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));
	}

	bool DirectAccumulation::IsActive(const Settings& settings) const
	{
		return settings.GeneralSettings.Mode == Mode::PathTracing &&
			(settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
				settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);
	}

	void DirectAccumulation::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void DirectAccumulation::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc bindingLayoutDesc;
		bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		bindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),              // LinearClampSampler
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // CameraData
			nvrhi::BindingLayoutItem::PushConstants(1, sizeof(DirectAccumulationPushConstants)),
			nvrhi::BindingLayoutItem::Texture_SRV(0),          // RawDirectDiffuse
			nvrhi::BindingLayoutItem::Texture_SRV(1),          // RawDirectSpecular
			nvrhi::BindingLayoutItem::Texture_SRV(2),          // MotionVectors
			nvrhi::BindingLayoutItem::Texture_SRV(3),          // ViewDepth
			nvrhi::BindingLayoutItem::Texture_SRV(4),          // HistoryDiffuse
			nvrhi::BindingLayoutItem::Texture_SRV(5),          // HistorySpecular
			nvrhi::BindingLayoutItem::Texture_SRV(6),          // HistoryViewZ
			nvrhi::BindingLayoutItem::Texture_UAV(0),          // OutAccumDiffuse
			nvrhi::BindingLayoutItem::Texture_UAV(1),          // OutAccumSpecular
			nvrhi::BindingLayoutItem::Texture_UAV(2)           // OutAccumViewZ
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(bindingLayoutDesc);
	}

	void DirectAccumulation::CreatePipeline()
	{
		if (!m_BindingLayout)
			CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/DirectAccumulation.hlsl", {}, ShaderStage::Compute);

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

	void DirectAccumulation::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* scene = Scene::GetSingleton();
		auto* renderer = GetRenderer();
		auto& textureManager = renderer->RenderTargetManager();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::PushConstants(1, sizeof(DirectAccumulationPushConstants)),
			nvrhi::BindingSetItem::Texture_SRV(0, textureManager.GetTexture(RenderTarget::DirectDiffuse)),
			nvrhi::BindingSetItem::Texture_SRV(1, textureManager.GetTexture(RenderTarget::DirectSpecular)),
			nvrhi::BindingSetItem::Texture_SRV(2, textureManager.GetTexture(RenderTarget::MotionVectors3D)),
			nvrhi::BindingSetItem::Texture_SRV(3, textureManager.GetTexture(RenderTarget::ViewDepth)),
			nvrhi::BindingSetItem::Texture_SRV(4, textureManager.GetTexture(RenderTarget::DirectDiffuseHistory)),
			nvrhi::BindingSetItem::Texture_SRV(5, textureManager.GetTexture(RenderTarget::DirectSpecularHistory)),
			nvrhi::BindingSetItem::Texture_SRV(6, textureManager.GetTexture(RenderTarget::DirectViewZHistory)),
			nvrhi::BindingSetItem::Texture_UAV(0, textureManager.GetTexture(RenderTarget::DirectDiffuseAccum)),
			nvrhi::BindingSetItem::Texture_UAV(1, textureManager.GetTexture(RenderTarget::DirectSpecularAccum)),
			nvrhi::BindingSetItem::Texture_UAV(2, textureManager.GetTexture(RenderTarget::DirectViewZAccum))
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void DirectAccumulation::SettingsChanged(const Settings& settings)
	{
		const bool active = IsActive(settings);
		if (active && !m_Enabled)
			m_ResetHistory = true;
		m_Enabled = active;

		// Any settings change potentially alters direct lighting: restart history.
		// The exponential blend self-heals within a few frames regardless.
		m_ResetHistory = true;
	}

	void DirectAccumulation::ResolutionChanged(uint2)
	{
		m_BindingSetDirty.fill(true);
		m_ResetHistory = true;
	}

	void DirectAccumulation::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		if (!m_ComputePipeline || !m_BindingSets[currentSlot])
			return;

		const auto revision = Scene::GetSingleton()->GetLightingRevision();
		if (m_LightingRevision != revision) {
			m_LightingRevision = revision;
			m_ResetHistory = true;
		}

		auto* renderer = GetRenderer();
		auto resolution = renderer->GetResolution();

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot] };
		commandList->setComputeState(state);

		DirectAccumulationPushConstants pushConstants;
		pushConstants.resetHistory = m_ResetHistory ? 1u : 0u;
		commandList->setPushConstants(&pushConstants, sizeof(pushConstants));

		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);

		// Copy the accumulated result into history for the next frame.
		auto& textureManager = renderer->RenderTargetManager();
		auto copyRegion = nvrhi::TextureSlice{ 0, 0, 0, resolution.x, resolution.y, 1 };
		commandList->copyTexture(
			textureManager.GetTexture(RenderTarget::DirectDiffuseHistory), copyRegion,
			textureManager.GetTexture(RenderTarget::DirectDiffuseAccum), copyRegion);
		commandList->copyTexture(
			textureManager.GetTexture(RenderTarget::DirectSpecularHistory), copyRegion,
			textureManager.GetTexture(RenderTarget::DirectSpecularAccum), copyRegion);
		commandList->copyTexture(
			textureManager.GetTexture(RenderTarget::DirectViewZHistory), copyRegion,
			textureManager.GetTexture(RenderTarget::DirectViewZAccum), copyRegion);

		m_ResetHistory = false;
	}
}
