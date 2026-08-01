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
		CreatePipeline();
	}

	void PostProcess::SettingsChanged(const Settings& settings)
	{
		auto generateSpecMV = (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR);

		auto writeDownscaled = (settings.GeneralSettings.Denoiser == Denoiser::NRD &&
		                          settings.GeneralSettings.Mode == Mode::GlobalIllumination &&
		                          settings.RaytracingSettings.ResolutionScale != 1.0f);

		m_Enabled = generateSpecMV || writeDownscaled;

		if (generateSpecMV != m_GenerateSpecularMotionVectors || writeDownscaled != m_WriteDownscaledInputs) {
			m_GenerateSpecularMotionVectors = generateSpecMV;
			m_WriteDownscaledInputs = writeDownscaled;

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
			nvrhi::BindingLayoutItem::Texture_SRV(0),            // DepthTexture
			nvrhi::BindingLayoutItem::Texture_SRV(1),            // NormalRoughnessTexture
			nvrhi::BindingLayoutItem::Texture_SRV(2),            // PrimaryMotionVectors
			nvrhi::BindingLayoutItem::Texture_SRV(3),            // SpecularHitDistanceTexture
			nvrhi::BindingLayoutItem::Texture_UAV(0),            // OutSpecularMotionVectors
			nvrhi::BindingLayoutItem::Texture_UAV(1),            // OutNormalRoughness
			nvrhi::BindingLayoutItem::Texture_UAV(2)             // OutMotionVectors
		};

		m_BindingLayout = device->createBindingLayout(bindingLayoutDesc);

		eastl::vector<DxcDefine> defines;
		if (m_GenerateSpecularMotionVectors)
			defines.push_back({ L"GENERATE_SPECULAR_MOTION_VECTORS", L"1" });

		if (m_WriteDownscaledInputs)
			defines.push_back({ L"WRITE_DOWNSCALED_NRD_INPUTS", L"1" });

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
			nvrhi::BindingSetItem::Texture_SRV(0, sourceDepth),
			nvrhi::BindingSetItem::Texture_SRV(1, renderTargets ? renderTargets->normalRoughness.Get() : nullptr),
			nvrhi::BindingSetItem::Texture_SRV(2, sourceMotionVectors),
			nvrhi::BindingSetItem::Texture_SRV(3, textureManager.GetTexture(RenderTarget::RRSpecularHitDist)),
			nvrhi::BindingSetItem::Texture_UAV(0, textureManager.GetTexture(RenderTarget::RRSpecularMotionVectors)),
			nvrhi::BindingSetItem::Texture_UAV(1, textureManager.GetTexture(RenderTarget::DownscaledNormalRoughness)),
			nvrhi::BindingSetItem::Texture_UAV(2, textureManager.GetTexture(RenderTarget::DownscaledMotionVectors))
		};

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
