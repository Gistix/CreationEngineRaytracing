#include "SHResolveTemporal.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "Util.h"

namespace Pass::Common
{
	SHResolveTemporal::SHResolveTemporal(Renderer* renderer, Pass::SceneTLAS* sceneTLAS)
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

	void SHResolveTemporal::Initialize()
	{
		CreatePipeline();
	}

	void SHResolveTemporal::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc bindingLayoutDesc;
		bindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		bindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // Raytracing
			nvrhi::BindingLayoutItem::PushConstants(2, sizeof(float)), // BlendAlpha
			nvrhi::BindingLayoutItem::Texture_SRV(0), // DiffuseRadiance
			nvrhi::BindingLayoutItem::Texture_SRV(1), // SpecularRadiance
			nvrhi::BindingLayoutItem::Texture_SRV(2), // DiffuseDirection
			nvrhi::BindingLayoutItem::Texture_SRV(3), // SpecularDirection
			nvrhi::BindingLayoutItem::Texture_SRV(4), // DownscaledMotionVectors
			nvrhi::BindingLayoutItem::Texture_SRV(5), // ViewDepth
			nvrhi::BindingLayoutItem::Texture_SRV(6), // DownscaledNormalRoughness
			nvrhi::BindingLayoutItem::Texture_SRV(7), // DiffuseSHHistory (array)
			nvrhi::BindingLayoutItem::Texture_SRV(8), // SpecularSHHistory (array)
			nvrhi::BindingLayoutItem::Texture_SRV(9), // HistoryDepth
			nvrhi::BindingLayoutItem::Texture_SRV(10), // HistoryNormal
			nvrhi::BindingLayoutItem::Texture_UAV(0), // DiffuseSHOut (array)
			nvrhi::BindingLayoutItem::Texture_UAV(1), // SpecularSHOut (array)
			nvrhi::BindingLayoutItem::Texture_UAV(2), // DepthOut
			nvrhi::BindingLayoutItem::Texture_UAV(3) // NormalOut
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(bindingLayoutDesc);
	}

	void SHResolveTemporal::CreatePipeline()
	{
		CreateBindingLayout();

		auto device = GetRenderer()->GetDevice();

		auto shaderBlob = ShaderCache::GetShader(L"data/shaders/SHResolveTemporal.hlsl", {}, ShaderStage::Compute);
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

	void SHResolveTemporal::CreateHistory(uint2 resolution)
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::TextureDesc shDesc{};
		shDesc.width = resolution.x;
		shDesc.height = resolution.y;
		shDesc.arraySize = SH_SLICES;
		shDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
		shDesc.format = nvrhi::Format::RGBA16_FLOAT;
		shDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		shDesc.isUAV = true;
		shDesc.keepInitialState = true;
		shDesc.debugName = "SHHistory";

		for (uint32_t lobe = 0; lobe < 2; lobe++)
			for (uint32_t slot = 0; slot < 2; slot++)
				m_SHHistory[lobe][slot] = device->createTexture(shDesc);

		nvrhi::TextureDesc depthDesc{};
		depthDesc.width = resolution.x;
		depthDesc.height = resolution.y;
		depthDesc.format = nvrhi::Format::R32_FLOAT;
		depthDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		depthDesc.isUAV = true;
		depthDesc.keepInitialState = true;
		depthDesc.debugName = "SHHistoryDepth";

		for (uint32_t slot = 0; slot < 2; slot++)
			m_HistoryDepth[slot] = device->createTexture(depthDesc);

		nvrhi::TextureDesc normalDesc{};
		normalDesc.width = resolution.x;
		normalDesc.height = resolution.y;
		normalDesc.format = nvrhi::Format::RGBA16_FLOAT;
		normalDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		normalDesc.isUAV = true;
		normalDesc.keepInitialState = true;
		normalDesc.debugName = "SHHistoryNormal";

		for (uint32_t slot = 0; slot < 2; slot++)
			m_HistoryNormal[slot] = device->createTexture(normalDesc);

		m_HistoryResolution = resolution;
	}

	void SHResolveTemporal::SettingsChanged(const Settings& settings)
	{
		const bool enabled = (settings.GeneralSettings.Denoiser == Denoiser::SH_Temporal);
		if (enabled && !m_Enabled)
			m_AccumulatedFrames = 0;
		m_Enabled = enabled;
	}

	void SHResolveTemporal::ResolutionChanged(uint2)
	{
		m_BindingSetDirty.fill(true);
		m_AccumulatedFrames = 0;
	}

	void SHResolveTemporal::CheckBindings()
	{
		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto& textureManager = renderer->RenderTargetManager();

		const auto resolution = renderer->GetScaledDynamicResolution();
		if (resolution.x != m_HistoryResolution.x || resolution.y != m_HistoryResolution.y)
		{
			CreateHistory(resolution);
			m_BindingSetDirty.fill(true);
			m_AccumulatedFrames = 0;
		}

		uint32_t currentSlot = renderer->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot][0])
			return;

		for (uint32_t parity = 0; parity < 2; parity++)
		{
			const uint32_t r = parity;
			const uint32_t w = parity ^ 1;

			nvrhi::BindingSetDesc bindingSetDesc;
			bindingSetDesc.bindings = {
				nvrhi::BindingSetItem::Sampler(0, m_LinearClampSampler),
				nvrhi::BindingSetItem::Sampler(1, m_PointClampSampler),
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
				nvrhi::BindingSetItem::PushConstants(2, sizeof(float)),
				nvrhi::BindingSetItem::Texture_SRV(0, textureManager.GetTexture(RenderTarget::DiffuseRadiance)),
				nvrhi::BindingSetItem::Texture_SRV(1, textureManager.GetTexture(RenderTarget::SpecularRadiance)),
				nvrhi::BindingSetItem::Texture_SRV(2, textureManager.GetTexture(RenderTarget::DiffuseDirection)),
				nvrhi::BindingSetItem::Texture_SRV(3, textureManager.GetTexture(RenderTarget::SpecularDirection)),
				nvrhi::BindingSetItem::Texture_SRV(4, textureManager.GetTexture(RenderTarget::DownscaledMotionVectors)),
				nvrhi::BindingSetItem::Texture_SRV(5, textureManager.GetTexture(RenderTarget::ViewDepth)),
				nvrhi::BindingSetItem::Texture_SRV(6, textureManager.GetTexture(RenderTarget::DownscaledNormalRoughness)),
				nvrhi::BindingSetItem::Texture_SRV(7, m_SHHistory[0][r].Get()),
				nvrhi::BindingSetItem::Texture_SRV(8, m_SHHistory[1][r].Get()),
				nvrhi::BindingSetItem::Texture_SRV(9, m_HistoryDepth[r].Get()),
				nvrhi::BindingSetItem::Texture_SRV(10, m_HistoryNormal[r].Get()),
				nvrhi::BindingSetItem::Texture_UAV(0, m_SHHistory[0][w].Get()),
				nvrhi::BindingSetItem::Texture_UAV(1, m_SHHistory[1][w].Get()),
				nvrhi::BindingSetItem::Texture_UAV(2, m_HistoryDepth[w].Get()),
				nvrhi::BindingSetItem::Texture_UAV(3, m_HistoryNormal[w].Get())
			};

			m_BindingSets[currentSlot][parity] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		}

		m_BindingSetDirty[currentSlot] = false;
	}

	void SHResolveTemporal::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		CheckBindings();

		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		const uint32_t setIndex = m_WriteIndex ^ 1;

		if (!m_ComputePipeline || !m_BindingSets[currentSlot][setIndex])
			return;

		const auto revision = Scene::GetSingleton()->GetLightingRevision();
		if (m_LightingRevision != revision) {
			m_LightingRevision = revision;
			m_AccumulatedFrames = 0;
		}

		const float blendAlpha = 1.0f / static_cast<float>(eastl::min(m_AccumulatedFrames + 1u, MAX_ACCUMULATED_FRAMES));

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = { m_BindingSets[currentSlot][setIndex] };
		commandList->setComputeState(state);

		commandList->setPushConstants(&blendAlpha, sizeof(blendAlpha));

		const auto resolution = GetRenderer()->GetScaledDynamicResolution();
		const auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);

		m_WriteIndex ^= 1;
		m_HistoryVersion++;
		m_AccumulatedFrames++;
	}
}
