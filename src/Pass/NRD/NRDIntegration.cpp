#include "NRDIntegration.h"

#include "Pass/Raytracing/Common/SceneTLAS.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderUtils.h"
#include "Utils/Game.h"

namespace Pass::NRD
{
	NRDIntegration::NRDIntegration(Renderer* renderer, nrd::Denoiser denoiser, Mode mode, Pass::SceneTLAS* sceneTLAS)
		: RenderPass(renderer), kDenoiser(denoiser), m_Mode(mode), m_SceneTLAS(sceneTLAS)
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

		kDenoiserIdentifier = nrd::Identifier(denoiser);

		m_ReblurSettings = {};
		m_ReblurSettings.checkerboardMode = nrd::CheckerboardMode::OFF;

		// Required when using probabilistic sampling
		m_ReblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;

		m_ReblurSettings.hitDistanceParameters.A = 3.0f * Util::Units::M_TO_GAME_UNIT;
		m_ReblurSettings.hitDistanceParameters.B = 0.1f * Util::Units::M_TO_GAME_UNIT;
		m_ReblurSettings.hitDistanceParameters.C = 20.0f;

		m_RelaxSettings = {};
		m_RelaxSettings.checkerboardMode = nrd::CheckerboardMode::OFF;

		// Required when using probabilistic sampling
		m_RelaxSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;

		m_SigmaSettings = {};

		SettingsChanged(Scene::GetSingleton()->m_Settings);
		Setup();
	}

	NRDIntegration::~NRDIntegration()
	{
		DestroyInstance();
	}

	void NRDIntegration::DestroyInstance()
	{
		if (m_NRD) {
			nrd::DestroyInstance(*m_NRD);
			m_NRD = nullptr;
		}

		m_Pipelines.clear();
		m_PermanentPool.clear();
		m_TransientPool.clear();
		m_DispatchBindingCaches.clear();
		m_GlobalBindingSet = nullptr;
		m_GlobalBindingLayout = nullptr;
		m_ResourceBindingLayout = nullptr;
		m_ConstantBuffer = nullptr;
		m_MotionVectorsScratch = nullptr;
		m_FallbackSrvTexture = nullptr;
		m_FallbackUavTexture = nullptr;
	}

	void NRDIntegration::Setup()
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
			logger::error("NRDIntegration: failed to create NRD instance ({})", uint32_t(result));
			return;
		}

		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);

		auto bufferDesc = nvrhi::BufferDesc()
			.setByteSize(instanceDesc.constantBufferMaxDataSize)
			.setIsConstantBuffer(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer)
			.setDebugName("NRD Reblur Constants");

		m_ConstantBuffer = GetRenderer()->GetDevice()->createBuffer(bufferDesc);
	}

	void NRDIntegration::Initialize()
	{
		CreateBindingLayouts();
		CreatePipelines();
		CreateResources();
		CreateGlobalBindingSet();
	}

	void NRDIntegration::CreateBindingLayouts()
	{
		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);

		nvrhi::BindingLayoutDesc globalLayoutDesc;
		globalLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalLayoutDesc.registerSpace = instanceDesc.constantBufferAndSamplersSpaceIndex;
		globalLayoutDesc.registerSpaceIsDescriptorSet = true;
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
		resourceLayoutDesc.registerSpaceIsDescriptorSet = true;

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

	uint32_t NRDIntegration::GetMaxResourceCount(nrd::DescriptorType type) const
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

	void NRDIntegration::CreatePipelines()
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
			}
			else {
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
					logger::error("NRDIntegration: invalid NRD shader identifier for pipeline {}", pipelineIndex);
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
				logger::error("NRDIntegration: failed to create shader for pipeline {}", pipelineIndex);
				continue;
			}

			nvrhi::ComputePipelineDesc computePipelineDesc;
			computePipelineDesc.setComputeShader(pipeline.shader);
			computePipelineDesc.addBindingLayout(m_GlobalBindingLayout);
			computePipelineDesc.addBindingLayout(m_ResourceBindingLayout);

			pipeline.pipeline = device->createComputePipeline(computePipelineDesc);
		}
	}

	void NRDIntegration::CreateResources()
	{
		if (!m_NRD)
			return;

		const nrd::InstanceDesc& instanceDesc = *nrd::GetInstanceDesc(*m_NRD);
		const uint2 resolution = GetRenderer()->GetResolution();
		auto device = GetRenderer()->GetDevice();

		m_DispatchBindingCaches.clear();

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
			if (m_Mode == Mode::GlobalIllumination)
				desc = GetRenderer()->GetMotionVectorTexture()->getDesc();
			else
				desc = GetRenderer()->RenderTargetManager().GetTexture(RenderTarget::MotionVectors3D)->getDesc();

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

	void NRDIntegration::CreateGlobalBindingSet()
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

	void NRDIntegration::SettingsChanged(const Settings& settings)
	{
		if (IsRelax())
			m_Enabled = (settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);
		else if (IsSigma())
			m_Enabled = (settings.GeneralSettings.ShadowDenoiser == ShadowDenoiser::NRD_Sigma) &&
				(settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur || settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax) &&
				!settings.SHaRCSettings.Enabled;
		else
			m_Enabled = (settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur);

		UpdateSettings(settings);
	}

	void NRDIntegration::UpdateSettings(const Settings& settings)
	{
		auto& commonSettings = settings.NRDSettings;

		if (IsRelax()) {
			auto& relaxSettings = settings.NRDRelaxSettings;

			m_RelaxSettings.diffuseMaxAccumulatedFrameNum = eastl::min(relaxSettings.diffuseMaxAccumulatedFrameNum, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
			m_RelaxSettings.specularMaxAccumulatedFrameNum = eastl::min(relaxSettings.specularMaxAccumulatedFrameNum, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
			m_RelaxSettings.diffuseMaxFastAccumulatedFrameNum =
				eastl::min(relaxSettings.diffuseMaxFastAccumulatedFrameNum, m_RelaxSettings.diffuseMaxAccumulatedFrameNum);
			m_RelaxSettings.specularMaxFastAccumulatedFrameNum =
				eastl::min(relaxSettings.specularMaxFastAccumulatedFrameNum, m_RelaxSettings.specularMaxAccumulatedFrameNum);
			m_RelaxSettings.historyFixFrameNum = eastl::min(commonSettings.historyFixFrameNum, 3u);
			m_RelaxSettings.historyFixBasePixelStride = eastl::max(commonSettings.historyFixBasePixelStride, 1u);
			m_RelaxSettings.historyFixAlternatePixelStride = eastl::max(commonSettings.historyFixAlternatePixelStride, 1u);
			m_RelaxSettings.fastHistoryClampingSigmaScale = eastl::clamp(commonSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f);
			m_RelaxSettings.diffusePrepassBlurRadius = eastl::max(commonSettings.diffusePrepassBlurRadius, 0.0f);
			m_RelaxSettings.specularPrepassBlurRadius = eastl::max(commonSettings.specularPrepassBlurRadius, 0.0f);
			m_RelaxSettings.minHitDistanceWeight = eastl::clamp(commonSettings.minHitDistanceWeight, 0.0001f, 0.2f);
			m_RelaxSettings.diffusePhiLuminance = eastl::max(relaxSettings.diffusePhiLuminance, 0.0f);
			m_RelaxSettings.specularPhiLuminance = eastl::max(relaxSettings.specularPhiLuminance, 0.0f);
			m_RelaxSettings.lobeAngleFraction = eastl::clamp(commonSettings.lobeAngleFraction, 0.0f, 1.0f);
			m_RelaxSettings.roughnessFraction = eastl::clamp(commonSettings.roughnessFraction, 0.0f, 1.0f);
			m_RelaxSettings.specularVarianceBoost = eastl::max(relaxSettings.specularVarianceBoost, 0.0f);
			m_RelaxSettings.specularLobeAngleSlack = eastl::max(relaxSettings.specularLobeAngleSlack, 0.0f);
			m_RelaxSettings.atrousIterationNum = eastl::clamp(relaxSettings.atrousIterationNum, 2u, 8u);
			m_RelaxSettings.depthThreshold = eastl::max(relaxSettings.depthThreshold, 0.0f);
			m_RelaxSettings.enableAntiFirefly = commonSettings.enableAntiFirefly;
			m_RelaxSettings.enableRoughnessEdgeStopping = relaxSettings.enableRoughnessEdgeStopping;
		}
		else if (IsSigma()) {
			auto& sigmaSettings = settings.NRDSigmaSettings;

			m_SigmaSettings.planeDistanceSensitivity = eastl::max(sigmaSettings.planeDistanceSensitivity, 0.0f);
			m_SigmaSettings.maxStabilizedFrameNum = eastl::min(sigmaSettings.maxStabilizedFrameNum, nrd::SIGMA_MAX_HISTORY_FRAME_NUM);
		}
		else {
			auto& reblurSettings = settings.NRDReblurSettings;

			m_ReblurSettings.maxAccumulatedFrameNum = eastl::min(reblurSettings.maxAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
			m_ReblurSettings.maxFastAccumulatedFrameNum = eastl::min(reblurSettings.maxFastAccumulatedFrameNum, m_ReblurSettings.maxAccumulatedFrameNum);
			m_ReblurSettings.maxStabilizedFrameNum = eastl::min(reblurSettings.maxStabilizedFrameNum, m_ReblurSettings.maxAccumulatedFrameNum);
			m_ReblurSettings.historyFixFrameNum = m_ReblurSettings.maxFastAccumulatedFrameNum > 0
				? eastl::min(commonSettings.historyFixFrameNum, m_ReblurSettings.maxFastAccumulatedFrameNum - 1)
				: 0;

			m_ReblurSettings.historyFixBasePixelStride = eastl::max(commonSettings.historyFixBasePixelStride, 1u);
			m_ReblurSettings.historyFixAlternatePixelStride = eastl::max(commonSettings.historyFixAlternatePixelStride, 1u);

			m_ReblurSettings.fastHistoryClampingSigmaScale = eastl::clamp(commonSettings.fastHistoryClampingSigmaScale, 1.0f, 3.0f);
			m_ReblurSettings.diffusePrepassBlurRadius = eastl::max(commonSettings.diffusePrepassBlurRadius, 0.0f);
			m_ReblurSettings.specularPrepassBlurRadius = eastl::max(commonSettings.specularPrepassBlurRadius, 0.0f);
			m_ReblurSettings.minHitDistanceWeight = eastl::clamp(commonSettings.minHitDistanceWeight, 0.0001f, 0.2f);
			m_ReblurSettings.minBlurRadius = eastl::max(reblurSettings.minBlurRadius, 0.0f);
			m_ReblurSettings.maxBlurRadius = eastl::max(reblurSettings.maxBlurRadius, m_ReblurSettings.minBlurRadius);
			m_ReblurSettings.lobeAngleFraction = eastl::clamp(commonSettings.lobeAngleFraction, 0.0f, 1.0f);
			m_ReblurSettings.roughnessFraction = eastl::clamp(commonSettings.roughnessFraction, 0.0f, 1.0f);
			m_ReblurSettings.planeDistanceSensitivity = eastl::max(reblurSettings.planeDistanceSensitivity, 0.0f);
			m_ReblurSettings.specularProbabilityThresholdsForMvModification[0] =
				eastl::clamp(reblurSettings.specularProbabilityThresholdsForMvModification[0], 0.0f, 1.0f);
			m_ReblurSettings.specularProbabilityThresholdsForMvModification[1] =
				eastl::clamp(reblurSettings.specularProbabilityThresholdsForMvModification[1],
					m_ReblurSettings.specularProbabilityThresholdsForMvModification[0], 1.0f);
			m_ReblurSettings.fireflySuppressorMinRelativeScale =
				eastl::clamp(reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f);
			m_ReblurSettings.enableAntiFirefly = commonSettings.enableAntiFirefly;
			m_ReblurSettings.usePrepassOnlyForSpecularMotionEstimation = reblurSettings.usePrepassOnlyForSpecularMotionEstimation;
			m_ReblurSettings.returnHistoryLengthInsteadOfOcclusion = reblurSettings.returnHistoryLengthInsteadOfOcclusion;
		}

		m_SettingsDirty = true;
	}

	void NRDIntegration::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_ResourcesDirty = true;
	}

	void NRDIntegration::UpdateCommonSettings()
	{
		auto* renderer = Renderer::GetSingleton();
		const auto& cameraRuntimeData = Scene::GetSingleton()->GetCameraRuntimeData();

		std::memcpy(m_CommonSettings.worldToViewMatrixPrev, m_CommonSettings.worldToViewMatrix, sizeof(float4x4));
		std::memcpy(m_CommonSettings.viewToClipMatrixPrev, m_CommonSettings.viewToClipMatrix, sizeof(float4x4));

		// Get camera's world position
		const float3& cameraWorldPos = cameraRuntimeData.posAdjust;

		// Create a translation matrix for the inverse of the camera's world position
		DirectX::XMMATRIX translationMat = DirectX::XMMatrixTranslation(-cameraWorldPos.x, -cameraWorldPos.y, -cameraWorldPos.z);

		// Combine view rotation with translation to get the full world-to-view matrix
		// NRD expects worldToViewMatrix to transform from world space to camera space, including translation.
		// So, V_full = ViewRotationMatrix * TranslationMatrix(-CameraWorldPosition)
		DirectX::XMMATRIX worldToViewMat = DirectX::XMMatrixMultiply(translationMat, cameraRuntimeData.viewMat);

		// Set full world to view
		std::memcpy(m_CommonSettings.worldToViewMatrix, &worldToViewMat, sizeof(float4x4));

		// Set original projection
		std::memcpy(m_CommonSettings.viewToClipMatrix, &cameraRuntimeData.projMat, sizeof(float4x4));

		const auto resolution = renderer->GetResolution();
		auto dynamicResolution = renderer->GetScaledDynamicResolution();

		m_CommonSettings.frameIndex = static_cast<uint32_t>(renderer->GetFrameIndex() % UINT32_MAX);

		auto jitter = renderer->GetJitter();
		m_CommonSettings.cameraJitter[0] = jitter.x;
		m_CommonSettings.cameraJitter[1] = jitter.y;

		auto prevJitter = renderer->GetPrevJitter();
		m_CommonSettings.cameraJitterPrev[0] = prevJitter.x;
		m_CommonSettings.cameraJitterPrev[1] = prevJitter.y;

		m_CommonSettings.motionVectorScale[0] = 1.0f;
		m_CommonSettings.motionVectorScale[1] = 1.0f;
		m_CommonSettings.motionVectorScale[2] = 0.0f;
		m_CommonSettings.isMotionVectorInWorldSpace = false;

		m_CommonSettings.resourceSizePrev[0] = m_CommonSettings.resourceSize[0];
		m_CommonSettings.resourceSizePrev[1] = m_CommonSettings.resourceSize[1];

		m_CommonSettings.resourceSize[0] = static_cast<uint16_t>(resolution.x);
		m_CommonSettings.resourceSize[1] = static_cast<uint16_t>(resolution.y);

		m_CommonSettings.rectSizePrev[0] = m_CommonSettings.rectSize[0];
		m_CommonSettings.rectSizePrev[1] = m_CommonSettings.rectSize[1];

		m_CommonSettings.rectSize[0] = static_cast<uint16_t>(dynamicResolution.x);
		m_CommonSettings.rectSize[1] = static_cast<uint16_t>(dynamicResolution.y);

		m_CommonSettings.denoisingRange = 1e6f;
	}

	bool NRDIntegration::IsDownscaled() const
	{
		return Scene::GetSingleton()->GetResolutionScale() != 1.0f;
	}

	nvrhi::ITexture* NRDIntegration::GetDispatchResource(const nrd::ResourceDesc& resource) const
	{
		auto* renderer = Renderer::GetSingleton();
		auto* renderTargets = renderer->GetRenderTargets();

		auto& textureManager = renderer->RenderTargetManager();

		auto* viewDepth = textureManager.GetTexture(RenderTarget::ViewDepth);
		auto* diffuseTexture = textureManager.GetTexture(RenderTarget::DiffuseRadiance);
		auto* specularTexture = textureManager.GetTexture(RenderTarget::SpecularRadiance);

		const bool downscaled = IsDownscaled();

		switch (resource.type) {
		case nrd::ResourceType::IN_MV:
			return downscaled ? textureManager.GetTexture(RenderTarget::DownscaledMotionVectors) : m_MotionVectorsScratch.Get();
		case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
			return downscaled ? textureManager.GetTexture(RenderTarget::DownscaledNormalRoughness) : renderTargets->normalRoughness.Get();
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
		case nrd::ResourceType::IN_PENUMBRA:
			return textureManager.GetTexture(RenderTarget::ShadowPenumbra);
		case nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY:
			return textureManager.GetTexture(RenderTarget::DenoisedShadow);
		case nrd::ResourceType::TRANSIENT_POOL:
			return resource.indexInPool < m_TransientPool.size() ? m_TransientPool[resource.indexInPool] : nullptr;
		case nrd::ResourceType::PERMANENT_POOL:
			return resource.indexInPool < m_PermanentPool.size() ? m_PermanentPool[resource.indexInPool] : nullptr;
		default:
			logger::error("NRDIntegration::GetDispatchResource - Requested Unmapped Resource: {}", magic_enum::enum_name(resource.type));
			return nullptr;
		}
	}

	nvrhi::Format NRDIntegration::GetFormat(nrd::Format format) const
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
			logger::error("NRDIntegration::GetFormat - Requested Unmapped Format: {}", magic_enum::enum_name(format));
			return nvrhi::Format::UNKNOWN;
		}
	}

	void NRDIntegration::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_NRD)
			return;

		const auto& generalSettings = Scene::GetSingleton()->m_Settings.GeneralSettings;
		if (IsRelax()) {
			if (generalSettings.Denoiser != Denoiser::NRD_Relax)
				return;
		}
		else if (IsSigma()) {
			if (generalSettings.ShadowDenoiser != ShadowDenoiser::NRD_Sigma ||
				(generalSettings.Denoiser != Denoiser::NRD_Reblur && generalSettings.Denoiser != Denoiser::NRD_Relax) ||
				Scene::GetSingleton()->m_Settings.SHaRCSettings.Enabled)
				return;
		}
		else {
			if (generalSettings.Denoiser != Denoiser::NRD_Reblur)
				return;
		}

		if (m_ResourcesDirty) {
			CreateResources();
			CreateGlobalBindingSet();
		}

		auto* renderer = GetRenderer();

		// When resolution scaling is active, PostProcess already wrote downscaled MVs to DownscaledMotionVectors
		const bool downscaled = IsDownscaled();

		if (!downscaled) {
			nvrhi::ITexture* sourceMotionVectors = nullptr;
			if (m_Mode == Mode::GlobalIllumination)
				sourceMotionVectors = renderer->GetMotionVectorTexture();
			else
				sourceMotionVectors = renderer->RenderTargetManager().GetTexture(RenderTarget::MotionVectors3D);

			if (sourceMotionVectors && m_MotionVectorsScratch) {
				const nvrhi::TextureDesc& mvDesc = sourceMotionVectors->getDesc();
				auto mvRegion = nvrhi::TextureSlice{ 0, 0, 0, mvDesc.width, mvDesc.height, 1 };
				commandList->copyTexture(m_MotionVectorsScratch, mvRegion, sourceMotionVectors, mvRegion);
			}
		}

		if (IsSigma()) {
			const auto& lightDir = m_SceneTLAS->GetRaytracingData()->DirectionalLight.Direction;
			if (m_SigmaSettings.lightDirection[0] != lightDir.x ||
				m_SigmaSettings.lightDirection[1] != lightDir.y ||
				m_SigmaSettings.lightDirection[2] != lightDir.z) {
				m_SigmaSettings.lightDirection[0] = lightDir.x;
				m_SigmaSettings.lightDirection[1] = lightDir.y;
				m_SigmaSettings.lightDirection[2] = lightDir.z;
				m_SettingsDirty = true;
			}
		}

		if (m_SettingsDirty) {
			if (IsRelax())
				nrd::SetDenoiserSettings(*m_NRD, kDenoiserIdentifier, &m_RelaxSettings);
			else if (IsSigma())
				nrd::SetDenoiserSettings(*m_NRD, kDenoiserIdentifier, &m_SigmaSettings);
			else
				nrd::SetDenoiserSettings(*m_NRD, kDenoiserIdentifier, &m_ReblurSettings);
			m_SettingsDirty = false;
		}

		UpdateCommonSettings();

		const nrd::Result commonSettingsResult = nrd::SetCommonSettings(*m_NRD, m_CommonSettings);
		if (commonSettingsResult != nrd::Result::SUCCESS) {
			logger::error("NRDIntegration: failed to set NRD common settings {}", magic_enum::enum_name(commonSettingsResult));
			return;
		}

		const nrd::Identifier identifiers[] = { kDenoiserIdentifier };
		const nrd::DispatchDesc* dispatchDescs = nullptr;
		uint32_t dispatchCount = 0;
		auto computeDispatchesResult = nrd::GetComputeDispatches(*m_NRD, identifiers, 1, dispatchDescs, dispatchCount);

		if (computeDispatchesResult != nrd::Result::SUCCESS) {
			logger::error("NRDIntegration: failed to get NRD dispatches {}", magic_enum::enum_name(computeDispatchesResult));
			return;
		}

		if (m_DispatchBindingCaches.size() != dispatchCount) {
			m_DispatchBindingCaches.clear();
			m_DispatchBindingCaches.resize(dispatchCount);
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

			eastl::vector<nvrhi::ITexture*> srvTextures(maxTextures, m_FallbackSrvTexture);
			eastl::vector<nvrhi::ITexture*> uavTextures(maxStorageTextures, m_FallbackUavTexture);
			uint32_t resourceCursor = 0;
			uint32_t srvBase = 0;
			uint32_t uavBase = 0;

			for (uint32_t rangeIndex = 0; rangeIndex < pipelineDesc.resourceRangesNum; ++rangeIndex) {
				const nrd::ResourceRangeDesc& rangeDesc = pipelineDesc.resourceRanges[rangeIndex];

				for (uint32_t rangeResourceIndex = 0; rangeResourceIndex < rangeDesc.descriptorsNum; ++rangeResourceIndex) {
					if (resourceCursor >= dispatchDesc.resourcesNum) {
						logger::error("NRDIntegration: resource range overflow for dispatch {}", dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
						return;
					}

					const nrd::ResourceDesc& resourceDesc = dispatchDesc.resources[resourceCursor++];
					nvrhi::ITexture* texture = GetDispatchResource(resourceDesc);

					if (!texture) {
						logger::warn("NRDIntegration: missing resource type {} for dispatch {}", nrd::GetResourceTypeString(resourceDesc.type), dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
						return;
					}

					if (rangeDesc.descriptorType == nrd::DescriptorType::TEXTURE) {
						if (srvBase + rangeResourceIndex >= srvTextures.size()) {
							logger::error("NRDIntegration: SRV range overflow for dispatch {}", dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
							return;
						}

						srvTextures[srvBase + rangeResourceIndex] = texture;
					}
					else {
						if (uavBase + rangeResourceIndex >= uavTextures.size()) {
							logger::error("NRDIntegration: UAV range overflow for dispatch {}", dispatchDesc.name ? dispatchDesc.name : "<unnamed>");
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

			auto& cache = m_DispatchBindingCaches[dispatchIndex];

			bool cacheValid = cache.bindingSet.Get() != nullptr &&
				cache.textures.size() == (srvTextures.size() + uavTextures.size());

			if (cacheValid) {
				size_t idx = 0;
				for (auto* tex : srvTextures) {
					if (cache.textures[idx++] != tex) { cacheValid = false; break; }
				}
				if (cacheValid) {
					for (auto* tex : uavTextures) {
						if (cache.textures[idx++] != tex) { cacheValid = false; break; }
					}
				}
			}

			if (!cacheValid) {
				nvrhi::BindingSetDesc resourceBindingDesc;

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

				cache.bindingSet = renderer->GetDevice()->createBindingSet(resourceBindingDesc, m_ResourceBindingLayout);
				cache.textures.clear();
				cache.textures.insert(cache.textures.end(), srvTextures.begin(), srvTextures.end());
				cache.textures.insert(cache.textures.end(), uavTextures.begin(), uavTextures.end());
			}

			nvrhi::ComputeState state;
			state.pipeline = pipeline.pipeline;
			state.bindings = { m_GlobalBindingSet, cache.bindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(dispatchDesc.gridWidth, dispatchDesc.gridHeight);

			commandList->endMarker();
		}
	}
}
