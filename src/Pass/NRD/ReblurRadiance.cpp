#include "ReblurRadiance.h"

#include "Renderer.h"
#include "Scene.h"
#include "ShaderUtils.h"

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

		m_ReblurSettings = {};
		m_ReblurSettings.checkerboardMode = nrd::CheckerboardMode::OFF;
		m_ReblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::OFF;
		m_ReblurSettings.maxAccumulatedFrameNum = eastl::min(30u, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
		m_ReblurSettings.enableAntiFirefly = false;

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
		m_MotionVectorsScratch = nullptr;
		m_FallbackSrvTexture = nullptr;
		m_FallbackUavTexture = nullptr;
	}

	void ReblurRadiance::Setup()
	{
		DestroyInstance();

		nrd::DenoiserDesc denoiserDesc = {};
		denoiserDesc.identifier = kDenoiserIdentifier;
		denoiserDesc.denoiser = kDenoiser;

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
				std::string shaderIdentifier = pipelineDesc.shaderIdentifier;
				eastl::vector<std::string> tokens;

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
					const std::string& token = tokens[tokenIndex];
					const size_t separator = token.find('=');

					defineNames.push_back(Util::StringToWString(separator == std::string::npos ? token : token.substr(0, separator)));
					defineValues.push_back(Util::StringToWString(separator == std::string::npos ? std::string("1") : token.substr(separator + 1)));
				}

				defines.reserve(defineNames.size());
				for (size_t defineIndex = 0; defineIndex < defineNames.size(); ++defineIndex)
					defines.push_back({ defineNames[defineIndex].c_str(), defineValues[defineIndex].c_str() });

				const std::wstring shaderPath = L"extern/NRD/Shaders/" + Util::StringToWString(tokens[0]);
				const std::wstring entryPoint = Util::StringToWString(std::string{ instanceDesc.shaderEntryPoint });
				ShaderUtils::CompileShader(shaderBlob, shaderPath.c_str(), defines, L"cs_6_5", entryPoint.c_str());

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
			nvrhi::ITexture* sourceMotionVectors = GetRenderer()->GetMotionVectorTexture();

			nvrhi::TextureDesc desc = sourceMotionVectors ? sourceMotionVectors->getDesc() : nvrhi::TextureDesc();

			desc.width = eastl::max<uint32_t>(1u, desc.width == 0 ? resolution.x : desc.width);
			desc.height = eastl::max<uint32_t>(1u, desc.height == 0 ? resolution.y : desc.height);
			desc.dimension = nvrhi::TextureDimension::Texture2D;
			desc.mipLevels = 1;
			desc.arraySize = 1;
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.debugName = "NRD Reblur Motion Vectors Scratch";
			m_MotionVectorsScratch = device->createTexture(desc);
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
		auto& reblurSettings = settings.ReblurSettings;

		m_ReblurSettings.maxAccumulatedFrameNum = eastl::min(reblurSettings.maxAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
		m_ReblurSettings.maxFastAccumulatedFrameNum = eastl::min(reblurSettings.maxFastAccumulatedFrameNum, m_ReblurSettings.maxAccumulatedFrameNum);
		m_ReblurSettings.maxStabilizedFrameNum = eastl::min(reblurSettings.maxStabilizedFrameNum, m_ReblurSettings.maxAccumulatedFrameNum);
		m_ReblurSettings.historyFixFrameNum = m_ReblurSettings.maxFastAccumulatedFrameNum > 0
			? eastl::min(reblurSettings.historyFixFrameNum, m_ReblurSettings.maxFastAccumulatedFrameNum - 1)
			: 0;

		m_ReblurSettings.historyFixBasePixelStride = eastl::max(reblurSettings.historyFixBasePixelStride, 1u);
		m_ReblurSettings.historyFixAlternatePixelStride = eastl::max(reblurSettings.historyFixAlternatePixelStride, 1u);

		m_ReblurSettings.fastHistoryClampingSigmaScale = eastl::clamp(reblurSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f);
		m_ReblurSettings.diffusePrepassBlurRadius = eastl::max(reblurSettings.diffusePrepassBlurRadius, 0.0f);
		m_ReblurSettings.specularPrepassBlurRadius = eastl::max(reblurSettings.specularPrepassBlurRadius, 0.0f);
		m_ReblurSettings.minHitDistanceWeight = eastl::clamp(reblurSettings.minHitDistanceWeight, 0.0001f, 0.2f);
		m_ReblurSettings.minBlurRadius = eastl::max(reblurSettings.minBlurRadius, 0.0f);
		m_ReblurSettings.maxBlurRadius = eastl::max(reblurSettings.maxBlurRadius, m_ReblurSettings.minBlurRadius);
		m_ReblurSettings.lobeAngleFraction = eastl::clamp(reblurSettings.lobeAngleFraction, 0.0f, 1.0f);
		m_ReblurSettings.roughnessFraction = eastl::clamp(reblurSettings.roughnessFraction, 0.0f, 1.0f);
		m_ReblurSettings.planeDistanceSensitivity = eastl::max(reblurSettings.planeDistanceSensitivity, 0.0f);
		m_ReblurSettings.specularProbabilityThresholdsForMvModification[0] =
			eastl::clamp(reblurSettings.specularProbabilityThresholdsForMvModification[0], 0.0f, 1.0f);
		m_ReblurSettings.specularProbabilityThresholdsForMvModification[1] =
			eastl::clamp(reblurSettings.specularProbabilityThresholdsForMvModification[1],
				m_ReblurSettings.specularProbabilityThresholdsForMvModification[0], 1.0f);
		m_ReblurSettings.fireflySuppressorMinRelativeScale =
			eastl::clamp(reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f);
		m_ReblurSettings.enableAntiFirefly = reblurSettings.enableAntiFirefly;
		m_ReblurSettings.usePrepassOnlyForSpecularMotionEstimation = reblurSettings.usePrepassOnlyForSpecularMotionEstimation;
		m_ReblurSettings.returnHistoryLengthInsteadOfOcclusion = reblurSettings.returnHistoryLengthInsteadOfOcclusion;

		m_SettingsDirty = true;
	}

	void ReblurRadiance::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_ResourcesDirty = true;
	}

	void ReblurRadiance::UpdateCommonSettings()
	{
		auto* renderer = Renderer::GetSingleton();

		auto& runtimeData = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();

		auto cameraData = runtimeData.cameraData.getEye();

		std::memcpy(m_CommonSettings.worldToViewMatrixPrev, m_CommonSettings.worldToViewMatrix, sizeof(float4x4));
		std::memcpy(m_CommonSettings.worldToViewMatrix, &cameraData.viewMat, sizeof(float4x4));

		std::memcpy(m_CommonSettings.viewToClipMatrixPrev, m_CommonSettings.viewToClipMatrix, sizeof(float4x4));
		std::memcpy(m_CommonSettings.viewToClipMatrix, &cameraData.projMat, sizeof(float4x4));

		const auto resolution = renderer->GetResolution();
		const auto dynamicResolution = renderer->GetDynamicResolution();

		m_CommonSettings.frameIndex = static_cast<uint32_t>(renderer->GetFrameIndex() % UINT32_MAX);

		auto jitter = renderer->GetJitter();
		m_CommonSettings.cameraJitter[0] = jitter.x;
		m_CommonSettings.cameraJitter[1] = jitter.y;

		auto prevJitter = renderer->GetJitter();
		m_CommonSettings.cameraJitterPrev[0] = prevJitter.x;
		m_CommonSettings.cameraJitterPrev[1] = prevJitter.y;

		m_CommonSettings.resourceSizePrev[0] = m_CommonSettings.resourceSize[0];
		m_CommonSettings.resourceSizePrev[1] = m_CommonSettings.resourceSize[1];

		m_CommonSettings.resourceSize[0] = static_cast<uint16_t>(resolution.x);
		m_CommonSettings.resourceSize[1] = static_cast<uint16_t>(resolution.y);

		m_CommonSettings.rectSizePrev[0] = m_CommonSettings.rectSize[0];
		m_CommonSettings.rectSizePrev[1] = m_CommonSettings.rectSize[1];

		m_CommonSettings.rectSize[0] = static_cast<uint16_t>(dynamicResolution.x);
		m_CommonSettings.rectSize[1] = static_cast<uint16_t>(dynamicResolution.y);
	}

	nvrhi::ITexture* ReblurRadiance::GetDispatchResource(const nrd::ResourceDesc& resource) const
	{
		auto* renderer = Renderer::GetSingleton();
		auto* renderTargets = renderer->GetRenderTargets();
		
		auto& textureManager = renderer->GetTextureManager();

		auto* viewDepth = textureManager.GetTexture(TextureManager::Texture::ViewDepth);
		auto* diffuseTexture = textureManager.GetTexture(TextureManager::Texture::DiffuseRadiance);
		auto* specularTexture = textureManager.GetTexture(TextureManager::Texture::SpecularRadiance);

		switch (resource.type) {
		case nrd::ResourceType::IN_MV:
			return m_MotionVectorsScratch;
		case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
			return renderTargets ? renderTargets->normalRoughness : nullptr;
		case nrd::ResourceType::IN_VIEWZ:
			return viewDepth;
		case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
			return diffuseTexture;
		case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
			return specularTexture;
		case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
			return diffuseTexture;
		case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
			return specularTexture;
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
		case nrd::Format::R32_UINT: return nvrhi::Format::R32_UINT;
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

		if (Scene::GetSingleton()->m_Settings.GeneralSettings.Denoiser != Denoiser::NRD_REBLUR)
			return;

		auto* renderer = GetRenderer();
		nvrhi::ITexture* sourceMotionVectors = renderer->GetMotionVectorTexture();

		if (m_ResourcesDirty) {
			CreateResources();
			CreateGlobalBindingSet();
		}

		if (sourceMotionVectors && m_MotionVectorsScratch) {
			const nvrhi::TextureDesc& mvDesc = sourceMotionVectors->getDesc();
			auto mvRegion = nvrhi::TextureSlice{ 0, 0, 0, mvDesc.width, mvDesc.height, 1 };
			commandList->copyTexture(m_MotionVectorsScratch, mvRegion, sourceMotionVectors, mvRegion);
		}

		if (m_SettingsDirty) {
			nrd::SetDenoiserSettings(*m_NRD, kDenoiserIdentifier, &m_ReblurSettings);
			m_SettingsDirty = false;
		}

		UpdateCommonSettings();

		const nrd::Result commonSettingsResult = nrd::SetCommonSettings(*m_NRD, m_CommonSettings);
		if (commonSettingsResult != nrd::Result::SUCCESS) {
			logger::error("ReblurRadiance: failed to set NRD common settings {}", magic_enum::enum_name(commonSettingsResult));
			return;
		}

		const nrd::Identifier identifiers[] = { kDenoiserIdentifier };
		const nrd::DispatchDesc* dispatchDescs = nullptr;
		uint32_t dispatchCount = 0;
		auto computeDispatchesResult = nrd::GetComputeDispatches(*m_NRD, identifiers, 1, dispatchDescs, dispatchCount);

		if (computeDispatchesResult != nrd::Result::SUCCESS) {
			logger::error("ReblurRadiance: failed to get NRD dispatches {}", magic_enum::enum_name(computeDispatchesResult));
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
			
			commandList->beginMarker(dispatchDesc.name);

			const nrd::PipelineDesc& pipelineDesc = instanceDesc.pipelines[dispatchDesc.pipelineIndex];

			if (dispatchDesc.constantBufferData && dispatchDesc.constantBufferDataSize > 0)
				commandList->writeBuffer(m_ConstantBuffer, dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);

			nvrhi::BindingSetDesc resourceBindingDesc;
			eastl::vector<nvrhi::ITexture*> srvTextures(maxTextures, m_FallbackSrvTexture);
			eastl::vector<nvrhi::ITexture*> uavTextures(maxStorageTextures, m_FallbackUavTexture);
			uint32_t resourceCursor = 0;
			uint32_t srvBase = 0;
			uint32_t uavBase = 0;

			for (uint32_t rangeIndex = 0; rangeIndex < pipelineDesc.resourceRangesNum; ++rangeIndex) {
				const nrd::ResourceRangeDesc& rangeDesc = pipelineDesc.resourceRanges[rangeIndex];

				for (uint32_t rangeResourceIndex = 0; rangeResourceIndex < rangeDesc.descriptorsNum; ++rangeResourceIndex) {
					if (resourceCursor >= dispatchDesc.resourcesNum) {
						logger::error("ReblurRadiance: resource range overflow for dispatch {}", dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
						return;
					}

					const nrd::ResourceDesc& resourceDesc = dispatchDesc.resources[resourceCursor++];
					nvrhi::ITexture* texture = GetDispatchResource(resourceDesc);

					if (!texture) {
						logger::warn("ReblurRadiance: missing resource type {} for dispatch {}", nrd::GetResourceTypeString(resourceDesc.type), dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
						return;
					}

					if (rangeDesc.descriptorType == nrd::DescriptorType::TEXTURE) {
						if (srvBase + rangeResourceIndex >= srvTextures.size()) {
							logger::error("ReblurRadiance: SRV range overflow for dispatch {}", dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
							return;
						}

						srvTextures[srvBase + rangeResourceIndex] = texture;
					} else {
						if (uavBase + rangeResourceIndex >= uavTextures.size()) {
							logger::error("ReblurRadiance: UAV range overflow for dispatch {}", dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
							return;
						}

						uavTextures[uavBase + rangeResourceIndex] = texture;
					}
				}

				if (rangeDesc.descriptorType == nrd::DescriptorType::TEXTURE)
					srvBase += rangeDesc.descriptorsNum;
				else
					uavBase += rangeDesc.descriptorsNum;
			}

			for (uint32_t textureIndex = 0; textureIndex < srvTextures.size(); ++textureIndex) {
				auto item = nvrhi::BindingSetItem::Texture_SRV(instanceDesc.resourcesBaseRegisterIndex, srvTextures[textureIndex]);
				item.arrayElement = textureIndex;
				resourceBindingDesc.bindings.push_back(item);
			}

			for (uint32_t storageTextureIndex = 0; storageTextureIndex < uavTextures.size(); ++storageTextureIndex) {
				auto item = nvrhi::BindingSetItem::Texture_UAV(instanceDesc.resourcesBaseRegisterIndex, uavTextures[storageTextureIndex]);
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

			commandList->endMarker();
		}
	}
}
