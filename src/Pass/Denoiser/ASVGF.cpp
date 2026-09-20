#include "ASVGF.h"
#include "Renderer.h"
#include "Scene.h"
#include "Util.h"
#include "ShaderUtils.h"

namespace Pass::Denoiser
{
	ASVGF::ASVGF(Renderer* renderer, Mode mode)
		: RenderPass(renderer), m_Mode(mode)
	{
		auto device = renderer->GetDevice();

		m_PointClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(false));

		m_LinearClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		m_ConstantBuffer = device->createBuffer(
			nvrhi::utils::CreateVolatileConstantBufferDesc(
				sizeof(ASVGFData), "ASVGF Data", 16));

		m_ASVGFSettings = Scene::GetSingleton()->m_Settings.ASVGFSettings;
		m_Enabled = (Scene::GetSingleton()->m_Settings.GeneralSettings.Denoiser == ::Denoiser::ASVGF);
	}

	void ASVGF::Initialize()
	{
		CreateBindingLayouts();
		CreatePipelines();
	}

	void ASVGF::CreateBindingLayouts()
	{
		auto device = GetRenderer()->GetDevice();

		// 1. Temporal Reprojection
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
				nvrhi::BindingLayoutItem::Sampler(0),
				nvrhi::BindingLayoutItem::Sampler(1),
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),
				nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Texture_SRV(3),
				nvrhi::BindingLayoutItem::Texture_SRV(4),
				nvrhi::BindingLayoutItem::Texture_SRV(5),
				nvrhi::BindingLayoutItem::Texture_SRV(6),
				nvrhi::BindingLayoutItem::Texture_UAV(0),
				nvrhi::BindingLayoutItem::Texture_UAV(1),
				nvrhi::BindingLayoutItem::Texture_UAV(2)
			};
			m_TemporalBindingLayout = device->createBindingLayout(desc);
		}

		// 2. Gradient Filter
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),
				nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Texture_UAV(0)
			};
			m_GradientFilterBindingLayout = device->createBindingLayout(desc);
		}

		// 3. Temporal Accumulation
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),
				nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Texture_SRV(3),
				nvrhi::BindingLayoutItem::Texture_SRV(4),
				nvrhi::BindingLayoutItem::Texture_SRV(5),
				nvrhi::BindingLayoutItem::Texture_UAV(0),
				nvrhi::BindingLayoutItem::Texture_UAV(1),
				nvrhi::BindingLayoutItem::Texture_UAV(2),
				nvrhi::BindingLayoutItem::Texture_UAV(3),
				nvrhi::BindingLayoutItem::Texture_UAV(4)
			};
			m_TemporalAccumBindingLayout = device->createBindingLayout(desc);
		}

		// 4. Variance Estimation
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),
				nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Texture_SRV(3),
				nvrhi::BindingLayoutItem::Texture_SRV(4),
				nvrhi::BindingLayoutItem::Texture_UAV(0)
			};
			m_VarianceEstimateBindingLayout = device->createBindingLayout(desc);
		}

		// 5. Atrous Wavelet Filter
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
				nvrhi::BindingLayoutItem::PushConstants(2, sizeof(AtrousPushConstants)),
				nvrhi::BindingLayoutItem::Texture_SRV(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),
				nvrhi::BindingLayoutItem::Texture_SRV(2),
				nvrhi::BindingLayoutItem::Texture_SRV(3),
				nvrhi::BindingLayoutItem::Texture_UAV(0),
				nvrhi::BindingLayoutItem::Texture_UAV(1)
			};
			m_AtrousBindingLayout = device->createBindingLayout(desc);
		}
	}

	void ASVGF::CreatePipelines()
	{
		auto device = GetRenderer()->GetDevice();

		auto compileCompute = [&](const wchar_t* path, nvrhi::BindingLayoutHandle layout, nvrhi::ShaderHandle& outShader, nvrhi::ComputePipelineHandle& outPipeline) {
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, path, {}, ShaderStage::Compute, L"main");
			if (blob) {
				outShader = device->createShader({ nvrhi::ShaderType::Compute, "", "main" }, blob->GetBufferPointer(), blob->GetBufferSize());
				if (outShader) {
					auto desc = nvrhi::ComputePipelineDesc()
						.setComputeShader(outShader)
						.addBindingLayout(layout);
					outPipeline = device->createComputePipeline(desc);
				}
			}
		};

		compileCompute(L"data/shaders/asvgf/ASVGF_Temporal.hlsl", m_TemporalBindingLayout, m_TemporalShader, m_TemporalPipeline);
		compileCompute(L"data/shaders/asvgf/ASVGF_GradientFilter.hlsl", m_GradientFilterBindingLayout, m_GradientFilterShader, m_GradientFilterPipeline);
		compileCompute(L"data/shaders/asvgf/ASVGF_TemporalAccumulation.hlsl", m_TemporalAccumBindingLayout, m_TemporalAccumShader, m_TemporalAccumPipeline);
		compileCompute(L"data/shaders/asvgf/ASVGF_VarianceEstimate.hlsl", m_VarianceEstimateBindingLayout, m_VarianceEstimateShader, m_VarianceEstimatePipeline);
		compileCompute(L"data/shaders/asvgf/ASVGF_Atrous.hlsl", m_AtrousBindingLayout, m_AtrousShader, m_AtrousPipeline);
	}

	void ASVGF::CreateResources()
	{
		auto device = GetRenderer()->GetDevice();
		auto resolution = GetRenderer()->GetResolution();

		auto makeTex = [&](nvrhi::Format format, const char* name) -> nvrhi::TextureHandle {
			nvrhi::TextureDesc desc{};
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.format = format;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.isUAV = true;
			desc.keepInitialState = true;
			desc.debugName = name;
			return device->createTexture(desc);
		};

		for (int s = 0; s < SignalType::SignalCount; ++s) {
			const char* prefix = (s == SignalType::Diffuse) ? "ASVGF Diffuse" : "ASVGF Specular";
			for (int i = 0; i < 2; ++i) {
				m_HistoryRadianceMoments[s][i] = makeTex(nvrhi::Format::RGBA32_FLOAT, fmt::format("{} History Radiance Moments", prefix).c_str());
				m_HistoryNormalDepth[s][i] = makeTex(nvrhi::Format::RGBA32_FLOAT, fmt::format("{} History Normal Depth", prefix).c_str());
				m_HistoryLength[s][i] = makeTex(nvrhi::Format::R16_FLOAT, fmt::format("{} History Length", prefix).c_str());
			}
		}

		for (int i = 0; i < 2; ++i) {
			m_RadiancePingPong[i] = makeTex(nvrhi::Format::RGBA16_FLOAT, "ASVGF Radiance PingPong");
			m_VariancePingPong[i] = makeTex(nvrhi::Format::R16_FLOAT, "ASVGF Variance PingPong");
		}

		m_ReprojectedHistory = makeTex(nvrhi::Format::RGBA32_FLOAT, "ASVGF Reprojected History");
		m_ReprojectedLength = makeTex(nvrhi::Format::R16_FLOAT, "ASVGF Reprojected Length");
		m_TemporalGradient = makeTex(nvrhi::Format::RG16_FLOAT, "ASVGF Temporal Gradient");
		m_FilteredGradient = makeTex(nvrhi::Format::RG16_FLOAT, "ASVGF Filtered Gradient");

		m_ResourcesDirty = false;
		m_BindingSetDirty.fill(true);
	}

	void ASVGF::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_ResourcesDirty = true;
		m_BindingSetDirty.fill(true);
	}

	void ASVGF::SettingsChanged(const Settings& settings)
	{
		m_ASVGFSettings = settings.ASVGFSettings;
		m_Enabled = (settings.GeneralSettings.Denoiser == ::Denoiser::ASVGF);
		m_BindingSetDirty.fill(true);
	}

	void ASVGF::ReloadShaders()
	{
		CreatePipelines();
		m_BindingSetDirty.fill(true);
	}

	void ASVGF::CheckBindings(uint32_t slot)
	{
		if (!m_BindingSetDirty[slot] && m_TemporalBindingSets[slot][0][0])
			return;

		auto device = GetRenderer()->GetDevice();
		auto* scene = Scene::GetSingleton();
		auto& textureManager = GetRenderer()->RenderTargetManager();

		auto* depthTexture = textureManager.GetTexture(RenderTarget::ViewDepth, slot);
		auto* normalRoughnessTexture = textureManager.GetTexture(RenderTarget::DownscaledNormalRoughness, slot);
		auto* motionVectorsTexture = textureManager.GetTexture(RenderTarget::DownscaledMotionVectors, slot);

		if (!depthTexture || !normalRoughnessTexture || !motionVectorsTexture)
			return;

		// 1. Gradient Filter
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
				nvrhi::BindingSetItem::Texture_SRV(0, m_TemporalGradient),
				nvrhi::BindingSetItem::Texture_SRV(1, depthTexture),
				nvrhi::BindingSetItem::Texture_SRV(2, normalRoughnessTexture),
				nvrhi::BindingSetItem::Texture_UAV(0, m_FilteredGradient)
			};
			m_GradientFilterBindingSets[slot] = device->createBindingSet(desc, m_GradientFilterBindingLayout);
		}

		// 2. Atrous Filter (dir 0: in 0 -> out 1; dir 1: in 1 -> out 0)
		for (uint32_t dir = 0; dir < 2; ++dir)
		{
			uint32_t inIdx = dir;
			uint32_t outIdx = 1 - dir;
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
				nvrhi::BindingSetItem::PushConstants(2, sizeof(AtrousPushConstants)),
				nvrhi::BindingSetItem::Texture_SRV(0, m_RadiancePingPong[inIdx]),
				nvrhi::BindingSetItem::Texture_SRV(1, m_VariancePingPong[inIdx]),
				nvrhi::BindingSetItem::Texture_SRV(2, depthTexture),
				nvrhi::BindingSetItem::Texture_SRV(3, normalRoughnessTexture),
				nvrhi::BindingSetItem::Texture_UAV(0, m_RadiancePingPong[outIdx]),
				nvrhi::BindingSetItem::Texture_UAV(1, m_VariancePingPong[outIdx])
			};
			m_AtrousBindingSets[slot][dir] = device->createBindingSet(desc, m_AtrousBindingLayout);
		}

		// 3. Per Signal (Diffuse, Specular) & History Permutation (0, 1)
		for (int s = 0; s < SignalType::SignalCount; ++s)
		{
			auto rt = (s == SignalType::Diffuse) ? RenderTarget::DiffuseRadiance : RenderTarget::SpecularRadiance;
			auto* inputRadiance = textureManager.GetTexture(rt, slot);

			for (int h = 0; h < 2; ++h)
			{
				// Temporal Reprojection (reads history h)
				{
					nvrhi::BindingSetDesc desc;
					desc.bindings = {
						nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
						nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
						nvrhi::BindingSetItem::Sampler(0, m_PointClampSampler),
						nvrhi::BindingSetItem::Sampler(1, m_LinearClampSampler),
						nvrhi::BindingSetItem::Texture_SRV(0, inputRadiance),
						nvrhi::BindingSetItem::Texture_SRV(1, depthTexture),
						nvrhi::BindingSetItem::Texture_SRV(2, normalRoughnessTexture),
						nvrhi::BindingSetItem::Texture_SRV(3, motionVectorsTexture),
						nvrhi::BindingSetItem::Texture_SRV(4, m_HistoryRadianceMoments[s][h]),
						nvrhi::BindingSetItem::Texture_SRV(5, m_HistoryNormalDepth[s][h]),
						nvrhi::BindingSetItem::Texture_SRV(6, m_HistoryLength[s][h]),
						nvrhi::BindingSetItem::Texture_UAV(0, m_ReprojectedHistory),
						nvrhi::BindingSetItem::Texture_UAV(1, m_ReprojectedLength),
						nvrhi::BindingSetItem::Texture_UAV(2, m_TemporalGradient)
					};
					m_TemporalBindingSets[slot][s][h] = device->createBindingSet(desc, m_TemporalBindingLayout);
				}

				// Temporal Accumulation (writes to history h)
				{
					nvrhi::BindingSetDesc desc;
					desc.bindings = {
						nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
						nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
						nvrhi::BindingSetItem::Texture_SRV(0, inputRadiance),
						nvrhi::BindingSetItem::Texture_SRV(1, m_ReprojectedHistory),
						nvrhi::BindingSetItem::Texture_SRV(2, m_ReprojectedLength),
						nvrhi::BindingSetItem::Texture_SRV(3, m_FilteredGradient),
						nvrhi::BindingSetItem::Texture_SRV(4, depthTexture),
						nvrhi::BindingSetItem::Texture_SRV(5, normalRoughnessTexture),
						nvrhi::BindingSetItem::Texture_UAV(0, m_RadiancePingPong[0]),
						nvrhi::BindingSetItem::Texture_UAV(1, m_VariancePingPong[0]),
						nvrhi::BindingSetItem::Texture_UAV(2, m_HistoryRadianceMoments[s][h]),
						nvrhi::BindingSetItem::Texture_UAV(3, m_HistoryNormalDepth[s][h]),
						nvrhi::BindingSetItem::Texture_UAV(4, m_HistoryLength[s][h])
					};
					m_TemporalAccumBindingSets[slot][s][h] = device->createBindingSet(desc, m_TemporalAccumBindingLayout);
				}

				// Variance Estimate (reads history length h)
				{
					nvrhi::BindingSetDesc desc;
					desc.bindings = {
						nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
						nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
						nvrhi::BindingSetItem::Texture_SRV(0, m_RadiancePingPong[0]),
						nvrhi::BindingSetItem::Texture_SRV(1, m_VariancePingPong[0]),
						nvrhi::BindingSetItem::Texture_SRV(2, m_HistoryLength[s][h]),
						nvrhi::BindingSetItem::Texture_SRV(3, depthTexture),
						nvrhi::BindingSetItem::Texture_SRV(4, normalRoughnessTexture),
						nvrhi::BindingSetItem::Texture_UAV(0, m_VariancePingPong[1])
					};
					m_VarianceEstimateBindingSets[slot][s][h] = device->createBindingSet(desc, m_VarianceEstimateBindingLayout);
				}
			}
		}

		m_BindingSetDirty[slot] = false;
	}

	void ASVGF::DenoiseSignal(
		nvrhi::ICommandList* commandList,
		uint32_t slot,
		nvrhi::ITexture* outputRadiance,
		uint2 resolution,
		bool isSpecular)
	{
		const uint32_t signalIdx = isSpecular ? SignalType::Specular : SignalType::Diffuse;
		const uint32_t prevHistory = m_HistoryIndex;
		const uint32_t nextHistory = 1 - m_HistoryIndex;

		ASVGFData cbData{};
		cbData.RenderSize = float2(static_cast<float>(resolution.x), static_cast<float>(resolution.y));
		cbData.InvRenderSize = float2(1.0f / cbData.RenderSize.x, 1.0f / cbData.RenderSize.y);
		cbData.DepthSigma = m_ASVGFSettings.DepthSigma;
		cbData.NormalSigma = isSpecular ? (m_ASVGFSettings.NormalSigma * 2.0f) : m_ASVGFSettings.NormalSigma;
		cbData.LuminanceSigma = m_ASVGFSettings.LuminanceSigma;
		cbData.TemporalAlphaMin = m_ASVGFSettings.TemporalAlphaMin;
		cbData.TemporalAlphaMax = m_ASVGFSettings.TemporalAlphaMax;
		cbData.GradientSensitivity = m_ASVGFSettings.GradientSensitivity;
		cbData.MaxHistoryLength = m_ASVGFSettings.MaxHistoryLength;
		cbData.DenoiseSpecular = m_ASVGFSettings.DenoiseSpecular ? 1 : 0;
		cbData.StepSize = 1;
		cbData.Iteration = 0;

		auto dispatchSize = Util::Math::GetDispatchCount(resolution, 8);

		// Pass 1: Temporal Reprojection
		if (m_TemporalPipeline && m_TemporalBindingSets[slot][signalIdx][prevHistory])
		{
			commandList->writeBuffer(m_ConstantBuffer, &cbData, sizeof(cbData));

			nvrhi::ComputeState state;
			state.pipeline = m_TemporalPipeline;
			state.bindings = { m_TemporalBindingSets[slot][signalIdx][prevHistory] };
			commandList->setComputeState(state);
			commandList->dispatch(dispatchSize.x, dispatchSize.y);
		}

		// Pass 2: Gradient Filter
		if (m_GradientFilterPipeline && m_GradientFilterBindingSets[slot])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_GradientFilterPipeline;
			state.bindings = { m_GradientFilterBindingSets[slot] };
			commandList->setComputeState(state);
			commandList->dispatch(dispatchSize.x, dispatchSize.y);
		}

		// Pass 3: Temporal Accumulation
		if (m_TemporalAccumPipeline && m_TemporalAccumBindingSets[slot][signalIdx][nextHistory])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_TemporalAccumPipeline;
			state.bindings = { m_TemporalAccumBindingSets[slot][signalIdx][nextHistory] };
			commandList->setComputeState(state);
			commandList->dispatch(dispatchSize.x, dispatchSize.y);
		}

		// Pass 4: Spatial Variance Estimation Prepass
		if (m_VarianceEstimatePipeline && m_VarianceEstimateBindingSets[slot][signalIdx][nextHistory])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_VarianceEstimatePipeline;
			state.bindings = { m_VarianceEstimateBindingSets[slot][signalIdx][nextHistory] };
			commandList->setComputeState(state);
			commandList->dispatch(dispatchSize.x, dispatchSize.y);

			// Copy variance back to pingpong 0
			commandList->copyTexture(m_VariancePingPong[0], nvrhi::TextureSlice(), m_VariancePingPong[1], nvrhi::TextureSlice());
		}

		// Pass 5: Multi-Scale Atrous Wavelet Filter
		uint32_t atrousInput = 0;
		uint32_t atrousOutput = 1;
		const int iterations = std::clamp(m_ASVGFSettings.AtrousIterations, 1, 5);

		for (int i = 0; i < iterations; ++i)
		{
			AtrousPushConstants pc{};
			pc.StepSize = 1 << i; // 1, 2, 4, 8, 16
			pc.Iteration = i;

			nvrhi::ComputeState state;
			state.pipeline = m_AtrousPipeline;
			state.bindings = { m_AtrousBindingSets[slot][atrousInput] };
			commandList->setComputeState(state);
			commandList->setPushConstants(&pc, sizeof(pc));
			commandList->dispatch(dispatchSize.x, dispatchSize.y);

			std::swap(atrousInput, atrousOutput);
		}

		// Copy final denoised result to destination
		commandList->copyTexture(outputRadiance, nvrhi::TextureSlice(), m_RadiancePingPong[atrousInput], nvrhi::TextureSlice());
	}

	void ASVGF::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		if (m_ResourcesDirty)
			CreateResources();

		if (m_ResourcesDirty)
			return;

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		CheckBindings(currentSlot);

		if (m_BindingSetDirty[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto& textureManager = renderer->RenderTargetManager();
		auto resolution = renderer->GetResolution();

		auto* diffuseRadiance = textureManager.GetTexture(RenderTarget::DiffuseRadiance, currentSlot);
		if (diffuseRadiance)
			DenoiseSignal(commandList, currentSlot, diffuseRadiance, resolution, false);

		if (m_ASVGFSettings.DenoiseSpecular)
		{
			auto* specularRadiance = textureManager.GetTexture(RenderTarget::SpecularRadiance, currentSlot);
			if (specularRadiance)
				DenoiseSignal(commandList, currentSlot, specularRadiance, resolution, true);
		}

		m_HistoryIndex = 1 - m_HistoryIndex;
	}
}
