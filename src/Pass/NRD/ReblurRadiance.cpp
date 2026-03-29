#include "ReblurRadiance.h"

#include "Renderer.h"
#include "Scene.h"
#include "ShaderUtils.h"

namespace
{
	constexpr auto kTarget = L"cs_6_5";

	std::wstring ToWide(const eastl::string& value)
	{
		return std::wstring(value.begin(), value.end());
	}

	void CopyMatrix(float* dst, const float4x4& src)
	{
		std::memcpy(dst, &src, sizeof(float) * 16);
	}
}

namespace Pass::NRD
{
	ReblurRadiance::ReblurRadiance(Renderer* renderer)
		: RenderPass(renderer)
	{
		auto device = GetRenderer()->GetDevice();

		m_NearestClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(false));

		m_LinearClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		SettingsChanged(Scene::GetSingleton()->m_Settings);
		Setup();
	}

	ReblurRadiance::~ReblurRadiance()
	{
		DestroyInstance();
	}

	void ReblurRadiance::DestroyInstance()
	{
		if (m_NRD) {
			nrd::DestroyInstance(*m_NRD);
			m_NRD = nullptr;
		}

		m_Pipelines.clear();
		m_PermanentPool.clear();
		m_TransientPool.clear();
		m_GlobalBindingSet = nullptr;
		m_GlobalBindingLayout = nullptr;
		m_ResourceBindingLayout = nullptr;
		m_ConstantBuffer = nullptr;
		m_DiffuseOutput = nullptr;
		m_ValidationOutput = nullptr;
		m_FallbackSrvTexture = nullptr;
		m_FallbackUavTexture = nullptr;
	}

	void ReblurRadiance::Setup()
	{
		DestroyInstance();

		nrd::DenoiserDesc denoiserDesc = {};
		denoiserDesc.identifier = kDenoiserIdentifier;
		denoiserDesc.denoiser = nrd::Denoiser::REBLUR_DIFFUSE;

		nrd::InstanceCreationDesc creationDesc = {};
		creationDesc.denoisers = &denoiserDesc;
		creationDesc.denoisersNum = 1;

		const nrd::Result result = nrd::CreateInstance(creationDesc, m_NRD);
		if (result != nrd::Result::SUCCESS || !m_NRD) {
			logger::error("ReblurRadiance: failed to create NRD instance ({})", uint32_t(result));
			return;
		}

		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);

		auto bufferDesc = nvrhi::BufferDesc()
			.setByteSize(instanceDesc.constantBufferMaxDataSize)
			.setIsConstantBuffer(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer)
			.setDebugName("NRD Reblur Constants");

		m_ConstantBuffer = GetRenderer()->GetDevice()->createBuffer(bufferDesc);

		CreateBindingLayouts();
		CreatePipelines();
		CreateResources();
		CreateGlobalBindingSet();
	}

	void ReblurRadiance::CreateBindingLayouts()
	{
		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);

		nvrhi::BindingLayoutDesc globalLayoutDesc;
		globalLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalLayoutDesc.registerSpace = instanceDesc.constantBufferAndSamplersSpaceIndex;
		globalLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::ConstantBuffer(instanceDesc.constantBufferRegisterIndex),
		};

		if (instanceDesc.samplersNum > 0) {
			auto samplers = nvrhi::BindingLayoutItem::Sampler(instanceDesc.samplersBaseRegisterIndex);
			samplers.setSize(instanceDesc.samplersNum);
			globalLayoutDesc.bindings.push_back(samplers);
		}

		m_GlobalBindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalLayoutDesc);

		nvrhi::BindingLayoutDesc resourceLayoutDesc;
		resourceLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		resourceLayoutDesc.registerSpace = instanceDesc.resourcesSpaceIndex;

		const uint32_t maxTextures = GetMaxResourceCount(nrd::DescriptorType::TEXTURE);
		const uint32_t maxStorageTextures = GetMaxResourceCount(nrd::DescriptorType::STORAGE_TEXTURE);

		if (maxTextures > 0) {
			auto textures = nvrhi::BindingLayoutItem::Texture_SRV(instanceDesc.resourcesBaseRegisterIndex);
			textures.setSize(maxTextures);
			resourceLayoutDesc.bindings.push_back(textures);
		}

		if (maxStorageTextures > 0) {
			auto storageTextures = nvrhi::BindingLayoutItem::Texture_UAV(instanceDesc.resourcesBaseRegisterIndex);
			storageTextures.setSize(maxStorageTextures);
			resourceLayoutDesc.bindings.push_back(storageTextures);
		}

		m_ResourceBindingLayout = GetRenderer()->GetDevice()->createBindingLayout(resourceLayoutDesc);
	}

	uint32_t ReblurRadiance::GetMaxResourceCount(nrd::DescriptorType type) const
	{
		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);
		uint32_t maxCount = 0;

		for (uint32_t pipelineIndex = 0; pipelineIndex < instanceDesc.pipelinesNum; ++pipelineIndex) {
			const nrd::PipelineDesc& pipelineDesc = instanceDesc.pipelines[pipelineIndex];

			for (uint32_t rangeIndex = 0; rangeIndex < pipelineDesc.resourceRangesNum; ++rangeIndex) {
				const nrd::ResourceRangeDesc& rangeDesc = pipelineDesc.resourceRanges[rangeIndex];
				if (rangeDesc.descriptorType == type)
					maxCount = eastl::max(maxCount, rangeDesc.descriptorsNum);
			}
		}

		return maxCount;
	}

	void ReblurRadiance::CreatePipelines()
	{
		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);
		auto device = GetRenderer()->GetDevice();

		m_Pipelines.clear();
		m_Pipelines.resize(instanceDesc.pipelinesNum);

		for (uint32_t pipelineIndex = 0; pipelineIndex < instanceDesc.pipelinesNum; ++pipelineIndex) {
			const nrd::PipelineDesc& pipelineDesc = instanceDesc.pipelines[pipelineIndex];
			auto& pipeline = m_Pipelines[pipelineIndex];
			pipeline.debugName = pipelineDesc.shaderIdentifier;

			winrt::com_ptr<IDxcBlob> shaderBlob;
			if (pipelineDesc.computeShaderDXIL.bytecode && pipelineDesc.computeShaderDXIL.size > 0) {
				pipeline.shader = device->createShader(
					{ nvrhi::ShaderType::Compute, pipeline.debugName.c_str(), instanceDesc.shaderEntryPoint },
					pipelineDesc.computeShaderDXIL.bytecode,
					size_t(pipelineDesc.computeShaderDXIL.size));
			} else {
				eastl::string shaderIdentifier = pipelineDesc.shaderIdentifier;
				eastl::vector<eastl::string> tokens;

				size_t start = 0;
				while (start <= shaderIdentifier.size()) {
					const size_t end = shaderIdentifier.find('|', start);
					if (end == eastl::string::npos) {
						tokens.push_back(shaderIdentifier.substr(start));
						break;
					}

					tokens.push_back(shaderIdentifier.substr(start, end - start));
					start = end + 1;
				}

				if (tokens.empty()) {
					logger::error("ReblurRadiance: invalid NRD shader identifier for pipeline {}", pipelineIndex);
					continue;
				}

				eastl::vector<std::wstring> defineNames;
				eastl::vector<std::wstring> defineValues;
				eastl::vector<DxcDefine> defines;

				for (size_t tokenIndex = 1; tokenIndex < tokens.size(); ++tokenIndex) {
					const eastl::string& token = tokens[tokenIndex];
					const size_t separator = token.find('=');

					defineNames.push_back(ToWide(separator == eastl::string::npos ? token : token.substr(0, separator)));
					defineValues.push_back(ToWide(separator == eastl::string::npos ? eastl::string("1") : token.substr(separator + 1)));
				}

				defines.reserve(defineNames.size());
				for (size_t defineIndex = 0; defineIndex < defineNames.size(); ++defineIndex)
					defines.push_back({ defineNames[defineIndex].c_str(), defineValues[defineIndex].c_str() });

				const std::wstring shaderPath = L"extern/NRD/Shaders/" + ToWide(tokens[0]);
				const std::wstring entryPoint = ToWide(instanceDesc.shaderEntryPoint);
				ShaderUtils::CompileShader(shaderBlob, shaderPath.c_str(), defines, kTarget, entryPoint.c_str());

				if (shaderBlob) {
					pipeline.shader = device->createShader(
						{ nvrhi::ShaderType::Compute, pipeline.debugName.c_str(), instanceDesc.shaderEntryPoint },
						shaderBlob->GetBufferPointer(),
						shaderBlob->GetBufferSize());
				}
			}

			if (!pipeline.shader) {
				logger::error("ReblurRadiance: failed to create shader for pipeline {}", pipelineIndex);
				continue;
			}

			nvrhi::ComputePipelineDesc computePipelineDesc;
			computePipelineDesc.setComputeShader(pipeline.shader);
			computePipelineDesc.addBindingLayout(m_GlobalBindingLayout);
			computePipelineDesc.addBindingLayout(m_ResourceBindingLayout);

			pipeline.pipeline = device->createComputePipeline(computePipelineDesc);
		}
	}

	void ReblurRadiance::CreateResources()
	{
		if (!m_NRD)
			return;

		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);
		const uint2 resolution = GetRenderer()->GetResolution();
		auto device = GetRenderer()->GetDevice();

		m_PermanentPool.clear();
		m_PermanentPool.resize(instanceDesc.permanentPoolSize);

		for (uint32_t i = 0; i < instanceDesc.permanentPoolSize; ++i) {
			const nrd::TextureDesc& textureDesc = instanceDesc.permanentPool[i];
			nvrhi::TextureDesc desc;
			const std::string debugName = std::format("NRD Reblur Permanent {}", i);
			desc.width = eastl::max<uint32_t>(1u, resolution.x / textureDesc.downsampleFactor);
			desc.height = eastl::max<uint32_t>(1u, resolution.y / textureDesc.downsampleFactor);
			desc.format = GetFormat(textureDesc.format);
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.debugName = debugName.c_str();
			m_PermanentPool[i] = device->createTexture(desc);
		}

		m_TransientPool.clear();
		m_TransientPool.resize(instanceDesc.transientPoolSize);

		for (uint32_t i = 0; i < instanceDesc.transientPoolSize; ++i) {
			const nrd::TextureDesc& textureDesc = instanceDesc.transientPool[i];
			nvrhi::TextureDesc desc;
			const std::string debugName = std::format("NRD Reblur Transient {}", i);
			desc.width = eastl::max<uint32_t>(1u, resolution.x / textureDesc.downsampleFactor);
			desc.height = eastl::max<uint32_t>(1u, resolution.y / textureDesc.downsampleFactor);
			desc.format = GetFormat(textureDesc.format);
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.debugName = debugName.c_str();
			m_TransientPool[i] = device->createTexture(desc);
		}

		{
			nvrhi::TextureDesc desc;
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.debugName = "NRD Reblur Diffuse Output";
			m_DiffuseOutput = device->createTexture(desc);
		}

		{
			nvrhi::TextureDesc desc;
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.format = nvrhi::Format::RGBA8_UNORM;
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.debugName = "NRD Reblur Validation Output";
			m_ValidationOutput = device->createTexture(desc);
		}

		{
			nvrhi::TextureDesc desc;
			desc.width = 1;
			desc.height = 1;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.debugName = "NRD Reblur Fallback SRV";
			m_FallbackSrvTexture = device->createTexture(desc);
		}

		{
			nvrhi::TextureDesc desc;
			desc.width = 1;
			desc.height = 1;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.debugName = "NRD Reblur Fallback UAV";
			m_FallbackUavTexture = device->createTexture(desc);
		}

		m_ResourcesDirty = false;
	}

	void ReblurRadiance::CreateGlobalBindingSet()
	{
		if (!m_NRD)
			return;

		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings.push_back(
			nvrhi::BindingSetItem::ConstantBuffer(instanceDesc.constantBufferRegisterIndex, m_ConstantBuffer));

		if (instanceDesc.samplersNum > 0) {
			auto nearestSampler = nvrhi::BindingSetItem::Sampler(instanceDesc.samplersBaseRegisterIndex, m_NearestClampSampler);
			nearestSampler.arrayElement = uint32_t(nrd::Sampler::NEAREST_CLAMP);
			bindingSetDesc.bindings.push_back(nearestSampler);

			if (instanceDesc.samplersNum > uint32_t(nrd::Sampler::LINEAR_CLAMP)) {
				auto linearSampler = nvrhi::BindingSetItem::Sampler(instanceDesc.samplersBaseRegisterIndex, m_LinearClampSampler);
				linearSampler.arrayElement = uint32_t(nrd::Sampler::LINEAR_CLAMP);
				bindingSetDesc.bindings.push_back(linearSampler);
			}
		}

		m_GlobalBindingSet = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_GlobalBindingLayout);
	}

	void ReblurRadiance::SettingsChanged([[maybe_unused]] const Settings& settings)
	{
		m_ReblurSettings = {};
		m_ReblurSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
		m_ReblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::OFF;
		m_ReblurSettings.maxAccumulatedFrameNum = eastl::min(30u, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
		m_ReblurSettings.maxFastAccumulatedFrameNum = 6;
		m_ReblurSettings.maxStabilizedFrameNum = 30;
		m_ReblurSettings.fastHistoryClampingSigmaScale = 1.5f;

		m_SettingsDirty = true;
	}

	void ReblurRadiance::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_ResourcesDirty = true;
	}

	nrd::CommonSettings ReblurRadiance::BuildCommonSettings() const
	{
		nrd::CommonSettings commonSettings = {};
		auto* renderer = Renderer::GetSingleton();

		const CameraData* camera = Scene::GetSingleton()->GetCameraData();
		if (!camera)
			return commonSettings;

		const float4x4 worldToView = camera->ViewInverse.Invert();
		const float4x4 worldToViewPrev = camera->PrevViewInverse.Invert();
		const float4x4 viewToClip = camera->ViewProj * camera->ViewInverse;
		const float4x4 viewToClipPrev = camera->PrevViewProj * camera->PrevViewInverse;

		CopyMatrix(commonSettings.worldToViewMatrix, worldToView);
		CopyMatrix(commonSettings.worldToViewMatrixPrev, worldToViewPrev);
		CopyMatrix(commonSettings.viewToClipMatrix, viewToClip);
		CopyMatrix(commonSettings.viewToClipMatrixPrev, viewToClipPrev);

		const uint2 resourceSize = renderer->GetResolution();
		const uint2 rectSize = (camera->RenderSize.x != 0 && camera->RenderSize.y != 0)
			? camera->RenderSize
			: resourceSize;

		commonSettings.frameIndex = camera->FrameIndex;
		commonSettings.cameraJitter[0] = camera->Jitter.x;
		commonSettings.cameraJitter[1] = camera->Jitter.y;
		commonSettings.cameraJitterPrev[0] = commonSettings.cameraJitter[0];
		commonSettings.cameraJitterPrev[1] = commonSettings.cameraJitter[1];
		commonSettings.resourceSize[0] = uint16_t(eastl::max(1u, resourceSize.x));
		commonSettings.resourceSize[1] = uint16_t(eastl::max(1u, resourceSize.y));
		commonSettings.resourceSizePrev[0] = commonSettings.resourceSize[0];
		commonSettings.resourceSizePrev[1] = commonSettings.resourceSize[1];
		commonSettings.rectSize[0] = uint16_t(eastl::max(1u, rectSize.x));
		commonSettings.rectSize[1] = uint16_t(eastl::max(1u, rectSize.y));
		commonSettings.rectSizePrev[0] = commonSettings.rectSize[0];
		commonSettings.rectSizePrev[1] = commonSettings.rectSize[1];
		commonSettings.motionVectorScale[0] = 1.0f;
		commonSettings.motionVectorScale[1] = 1.0f;
		commonSettings.motionVectorScale[2] = 0.0f;

		commonSettings.splitScreen = 1.0f;

		return commonSettings;
	}

	nvrhi::ITexture* ReblurRadiance::GetDispatchResource(const nrd::ResourceDesc& resource) const
	{
		auto* renderer = Renderer::GetSingleton();
		auto* renderTargets = renderer->GetRenderTargets();

		switch (resource.type) {
		case nrd::ResourceType::IN_MV:
			return Scene::GetSingleton()->IsPathTracingActive() ? renderer->m_PTMotionVectors.Get() : renderer->GetMotionVectorTexture();
		case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
			return renderTargets ? renderTargets->normalRoughness : nullptr;
		case nrd::ResourceType::IN_VIEWZ:
			return Scene::GetSingleton()->IsPathTracingActive() ? renderer->m_PTDepth.Get() : renderer->GetDepthTexture();
		case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
			return renderer->GetMainTexture();
		case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
			return m_DiffuseOutput;
		case nrd::ResourceType::OUT_VALIDATION:
			return m_ValidationOutput;
		case nrd::ResourceType::TRANSIENT_POOL:
			return resource.indexInPool < m_TransientPool.size() ? m_TransientPool[resource.indexInPool] : nullptr;
		case nrd::ResourceType::PERMANENT_POOL:
			return resource.indexInPool < m_PermanentPool.size() ? m_PermanentPool[resource.indexInPool] : nullptr;
		default:
			logger::error("ReblurRadiance::GetDispatchResource - Requested Unmapped Resource: {}", magic_enum::enum_name(resource.type));
			return nullptr;
		}
	}

	nvrhi::Format ReblurRadiance::GetFormat(nrd::Format format) const
	{
		switch (format) {
		case nrd::Format::R8_UINT: return nvrhi::Format::R8_UINT;
		case nrd::Format::R8_UNORM: return nvrhi::Format::R8_UNORM;
		case nrd::Format::RG8_UNORM: return nvrhi::Format::RG8_UNORM;
		case nrd::Format::RGBA8_UNORM: return nvrhi::Format::RGBA8_UNORM;
		case nrd::Format::R16_UINT: return nvrhi::Format::R16_UINT;
		case nrd::Format::R16_SFLOAT: return nvrhi::Format::R16_FLOAT;
		case nrd::Format::RG16_SFLOAT: return nvrhi::Format::RG16_FLOAT;
		case nrd::Format::RGBA16_SFLOAT: return nvrhi::Format::RGBA16_FLOAT;
		case nrd::Format::R32_SFLOAT: return nvrhi::Format::R32_FLOAT;
		case nrd::Format::RG32_SFLOAT: return nvrhi::Format::RG32_FLOAT;
		case nrd::Format::RGBA32_SFLOAT: return nvrhi::Format::RGBA32_FLOAT;
		case nrd::Format::R10_G10_B10_A2_UNORM: return nvrhi::Format::R10G10B10A2_UNORM;
		case nrd::Format::R11_G11_B10_UFLOAT: return nvrhi::Format::R11G11B10_FLOAT;
		default:
			logger::error("ReblurRadiance::GetFormat - Requested Unmapped Format: {}", magic_enum::enum_name(format));
			return nvrhi::Format::UNKNOWN;
		}
	}

	void ReblurRadiance::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_NRD)
			return;

		auto* renderer = GetRenderer();

		if (m_ResourcesDirty) {
			CreateResources();
			CreateGlobalBindingSet();
		}

		if (m_SettingsDirty) {
			nrd::SetDenoiserSettings(*m_NRD, kDenoiserIdentifier, &m_ReblurSettings);
			m_SettingsDirty = false;
		}

		const nrd::CommonSettings commonSettings = BuildCommonSettings();
		const nrd::Result commonSettingsResult = nrd::SetCommonSettings(*m_NRD, commonSettings);
		if (commonSettingsResult != nrd::Result::SUCCESS) {
			logger::error(
				"ReblurRadiance: failed to set NRD common settings (resource={}x{}, rect={}x{}, jitter=({}, {}), prevJitter=({}, {}), result={})",
				commonSettings.resourceSize[0], commonSettings.resourceSize[1],
				commonSettings.rectSize[0], commonSettings.rectSize[1],
				commonSettings.cameraJitter[0], commonSettings.cameraJitter[1],
				commonSettings.cameraJitterPrev[0], commonSettings.cameraJitterPrev[1],
				uint32_t(commonSettingsResult));
			return;
		}

		const nrd::Identifier identifiers[] = { kDenoiserIdentifier };
		const nrd::DispatchDesc* dispatchDescs = nullptr;
		uint32_t dispatchCount = 0;

		if (nrd::GetComputeDispatches(*m_NRD, identifiers, 1, dispatchDescs, dispatchCount) != nrd::Result::SUCCESS) {
			logger::error("ReblurRadiance: failed to get NRD dispatches");
			return;
		}

		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);
		const uint32_t maxTextures = GetMaxResourceCount(nrd::DescriptorType::TEXTURE);
		const uint32_t maxStorageTextures = GetMaxResourceCount(nrd::DescriptorType::STORAGE_TEXTURE);

		for (uint32_t dispatchIndex = 0; dispatchIndex < dispatchCount; ++dispatchIndex) {
			const nrd::DispatchDesc& dispatchDesc = dispatchDescs[dispatchIndex];
			if (dispatchDesc.pipelineIndex >= m_Pipelines.size())
				continue;

			const Pipeline& pipeline = m_Pipelines[dispatchDesc.pipelineIndex];
			if (!pipeline.pipeline)
				continue;

			if (dispatchDesc.constantBufferData && dispatchDesc.constantBufferDataSize > 0)
				commandList->writeBuffer(m_ConstantBuffer, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);

			nvrhi::BindingSetDesc resourceBindingDesc;
			uint32_t textureIndex = 0;
			uint32_t storageTextureIndex = 0;

			for (uint32_t resourceIndex = 0; resourceIndex < dispatchDesc.resourcesNum; ++resourceIndex) {
				const nrd::ResourceDesc& resourceDesc = dispatchDesc.resources[resourceIndex];
				nvrhi::ITexture* texture = GetDispatchResource(resourceDesc);

				if (!texture) {
					logger::warn("ReblurRadiance: missing resource type {} for dispatch {}", nrd::GetResourceTypeString(resourceDesc.type), dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
					return;
				}

				if (resourceDesc.descriptorType == nrd::DescriptorType::TEXTURE) {
					auto item = nvrhi::BindingSetItem::Texture_SRV(instanceDesc.resourcesBaseRegisterIndex, texture);
					item.arrayElement = textureIndex++;
					resourceBindingDesc.bindings.push_back(item);
				} else {
					auto item = nvrhi::BindingSetItem::Texture_UAV(instanceDesc.resourcesBaseRegisterIndex, texture);
					item.arrayElement = storageTextureIndex++;
					resourceBindingDesc.bindings.push_back(item);
				}
			}

			for (; textureIndex < maxTextures; ++textureIndex) {
				auto item = nvrhi::BindingSetItem::Texture_SRV(instanceDesc.resourcesBaseRegisterIndex, m_FallbackSrvTexture);
				item.arrayElement = textureIndex;
				resourceBindingDesc.bindings.push_back(item);
			}

			for (; storageTextureIndex < maxStorageTextures; ++storageTextureIndex) {
				auto item = nvrhi::BindingSetItem::Texture_UAV(instanceDesc.resourcesBaseRegisterIndex, m_FallbackUavTexture);
				item.arrayElement = storageTextureIndex;
				resourceBindingDesc.bindings.push_back(item);
			}

			nvrhi::BindingSetHandle resourceBindingSet =
				renderer->GetDevice()->createBindingSet(resourceBindingDesc, m_ResourceBindingLayout);

			nvrhi::ComputeState state;
			state.pipeline = pipeline.pipeline;
			state.bindings = { m_GlobalBindingSet, resourceBindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(dispatchDesc.gridWidth, dispatchDesc.gridHeight);
		}

		auto resolution = renderer->GetResolution();
		auto region = nvrhi::TextureSlice{ 0, 0, 0, resolution.x, resolution.y, 1 };
		commandList->copyTexture(renderer->GetMainTexture(), region, m_DiffuseOutput, region);
	}
}
