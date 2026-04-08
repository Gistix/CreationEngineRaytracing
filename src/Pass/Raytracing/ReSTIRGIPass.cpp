#include "ReSTIRGIPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "Constants.h"

#include "ReSTIRGIData.hlsli"
#include "Utils/Shader.h"

namespace Pass::Raytracing
{
	ReSTIRGIPass::ReSTIRGIPass(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		m_Defines = Util::Shader::GetRaytracingDefines(Scene::GetSingleton()->m_Settings, false, false);

		auto resolution = renderer->GetResolution();

		rtxdi::ReSTIRGIStaticParameters staticParams;
		staticParams.RenderWidth = resolution.x;
		staticParams.RenderHeight = resolution.y;
		staticParams.CheckerboardSamplingMode = rtxdi::CheckerboardMode::Off;

		m_Context = eastl::make_unique<rtxdi::ReSTIRGIContext>(staticParams);

		m_LinearWrapSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_ConstantBuffer = renderer->GetDevice()->createBuffer(
			nvrhi::utils::CreateVolatileConstantBufferDesc(
				sizeof(ReSTIRGIData), "ReSTIR GI Data", Constants::MAX_CB_VERSIONS));

		CreateBindingLayout();
		CreatePipeline();
	}

	void ReSTIRGIPass::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),    // b0: CameraData
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),    // b1: ReSTIRGIData
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),    // b2: FeatureData
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),     // t0: SceneBVH
			nvrhi::BindingLayoutItem::Texture_SRV(1),               // t1: CurrentDepth
			nvrhi::BindingLayoutItem::Texture_SRV(2),               // t2: CurrentNormals
			nvrhi::BindingLayoutItem::Texture_SRV(3),               // t3: PreviousDepth
			nvrhi::BindingLayoutItem::Texture_SRV(4),               // t4: PreviousNormals
			nvrhi::BindingLayoutItem::Texture_SRV(5),               // t5: SecondaryPositionNormal
			nvrhi::BindingLayoutItem::Texture_SRV(6),               // t6: SecondaryRadiance
			nvrhi::BindingLayoutItem::Texture_SRV(7),               // t7: SecondaryDiffuseAlbedo
			nvrhi::BindingLayoutItem::Texture_SRV(8),               // t8: SecondarySpecularRoughness
			nvrhi::BindingLayoutItem::TypedBuffer_SRV(9),           // t9: NeighborOffsets
			nvrhi::BindingLayoutItem::Texture_SRV(10),              // t10: MotionVectors
			nvrhi::BindingLayoutItem::Texture_SRV(11),              // t11: PrimaryDiffuseAlbedo
			nvrhi::BindingLayoutItem::Texture_SRV(12),              // t12: PrimarySpecularAlbedo
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13),     // t13: SurfaceDataBuffer (packed primary surface ping-pong)
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),      // u0: GIReservoirs
			nvrhi::BindingLayoutItem::Texture_UAV(1),               // u1: OutputRadiance
			nvrhi::BindingLayoutItem::Sampler(0),                   // s0
		};
		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void ReSTIRGIPass::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(DxcDefine{ L"USE_RAY_QUERY", L"1" });

		// Temporal Resampling
		{
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/RTXDI/ReSTIRGI/GITemporalResampling.hlsl", defines, L"cs_6_5");
			if (blob) {
				m_TemporalShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
				m_TemporalPipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc()
					.setComputeShader(m_TemporalShader)
					.addBindingLayout(m_BindingLayout));
			}
		}

		// Spatial Resampling
		{
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/RTXDI/ReSTIRGI/GISpatialResampling.hlsl", defines, L"cs_6_5");
			if (blob) {
				m_SpatialShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
				m_SpatialPipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc()
					.setComputeShader(m_SpatialShader)
					.addBindingLayout(m_BindingLayout));
			}
		}

		// Fused Spatiotemporal Resampling
		{
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/RTXDI/ReSTIRGI/GIFusedResampling.hlsl", defines, L"cs_6_5");
			if (blob) {
				m_FusedShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
				m_FusedPipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc()
					.setComputeShader(m_FusedShader)
					.addBindingLayout(m_BindingLayout));
			}
		}

		// Final Shading
		{
			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/RTXDI/ReSTIRGI/GIFinalShading.hlsl", defines, L"cs_6_5");
			if (blob) {
				m_FinalShadingShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
				m_FinalShadingPipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc()
					.setComputeShader(m_FinalShadingShader)
					.addBindingLayout(m_BindingLayout));
			}
		}
	}

	void ReSTIRGIPass::ResolutionChanged(uint2 resolution)
	{
		rtxdi::ReSTIRGIStaticParameters staticParams;
		staticParams.RenderWidth = resolution.x;
		staticParams.RenderHeight = resolution.y;
		staticParams.CheckerboardSamplingMode = rtxdi::CheckerboardMode::Off;

		m_Context = eastl::make_unique<rtxdi::ReSTIRGIContext>(staticParams);
		m_DirtyBindings = true;
	}

	void ReSTIRGIPass::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetRaytracingDefines(settings, false, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_DirtyBindings = true;
		}

		m_Enabled = settings.ReSTIRGI.Enabled;

		if (!m_Enabled)
			return;

		m_ResamplingMode = static_cast<rtxdi::ReSTIRGI_ResamplingMode>(settings.ReSTIRGI.ResamplingMode);
		m_Context->SetResamplingMode(m_ResamplingMode);

		// Temporal parameters
		{
			RTXDI_GITemporalResamplingParameters tparams = rtxdi::GetDefaultReSTIRGITemporalResamplingParams();
			tparams.depthThreshold = settings.ReSTIRGI.TemporalDepthThreshold;
			tparams.normalThreshold = settings.ReSTIRGI.TemporalNormalThreshold;
			tparams.maxHistoryLength = settings.ReSTIRGI.MaxHistoryLength;
			tparams.maxReservoirAge = settings.ReSTIRGI.MaxReservoirAge;
			tparams.enablePermutationSampling = settings.ReSTIRGI.EnablePermutationSampling ? 1 : 0;
			tparams.enableFallbackSampling = settings.ReSTIRGI.EnableFallbackSampling ? 1 : 0;
			tparams.biasCorrectionMode = static_cast<RTXDI_GIBiasCorrectionMode>(settings.ReSTIRGI.TemporalBiasCorrection);
			m_Context->SetTemporalResamplingParameters(tparams);
		}

		// Spatial parameters
		{
			RTXDI_GISpatialResamplingParameters sparams = rtxdi::GetDefaultReSTIRGISpatialResamplingParams();
			sparams.depthThreshold = settings.ReSTIRGI.SpatialDepthThreshold;
			sparams.normalThreshold = settings.ReSTIRGI.SpatialNormalThreshold;
			sparams.numSamples = settings.ReSTIRGI.SpatialNumSamples;
			sparams.samplingRadius = settings.ReSTIRGI.SpatialSamplingRadius;
			sparams.biasCorrectionMode = static_cast<RTXDI_GIBiasCorrectionMode>(settings.ReSTIRGI.SpatialBiasCorrection);
			m_Context->SetSpatialResamplingParameters(sparams);
		}

		// Spatiotemporal fused parameters
		{
			RTXDI_GISpatioTemporalResamplingParameters stparams = rtxdi::GetDefaultReSTIRGISpatioTemporalResamplingParams();
			stparams.depthThreshold = settings.ReSTIRGI.TemporalDepthThreshold;
			stparams.normalThreshold = settings.ReSTIRGI.TemporalNormalThreshold;
			stparams.maxHistoryLength = settings.ReSTIRGI.MaxHistoryLength;
			stparams.maxReservoirAge = settings.ReSTIRGI.MaxReservoirAge;
			stparams.enablePermutationSampling = settings.ReSTIRGI.EnablePermutationSampling ? 1 : 0;
			stparams.enableFallbackSampling = settings.ReSTIRGI.EnableFallbackSampling ? 1 : 0;
			stparams.biasCorrectionMode = static_cast<RTXDI_GIBiasCorrectionMode>(settings.ReSTIRGI.TemporalBiasCorrection);
			stparams.numSamples = settings.ReSTIRGI.SpatialNumSamples;
			stparams.samplingRadius = settings.ReSTIRGI.SpatialSamplingRadius;
			m_Context->SetSpatioTemporalResamplingParameters(stparams);
		}

		// Boiling filter
		{
			RTXDI_BoilingFilterParameters bparams = rtxdi::GetDefaultReSTIRGIBoilingFilterParams();
			bparams.enableBoilingFilter = settings.ReSTIRGI.EnableBoilingFilter ? 1 : 0;
			bparams.boilingFilterStrength = settings.ReSTIRGI.BoilingFilterStrength;
			m_Context->SetBoilingFilterParameters(bparams);
		}

		// Final shading
		{
			RTXDI_GIFinalShadingParameters fparams = rtxdi::GetDefaultReSTIRGIFinalShadingParams();
			fparams.enableFinalVisibility = settings.ReSTIRGI.EnableFinalVisibility ? 1 : 0;
			fparams.enableFinalMIS = settings.ReSTIRGI.EnableFinalMIS ? 1 : 0;
			m_Context->SetFinalShadingParameters(fparams);
		}
	}

	void ReSTIRGIPass::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto* giRes = renderer->GetReSTIRGIResources();
		auto* renderTargets = renderer->GetRenderTargets();
		auto* rrInput = renderer->GetRRInput();

		auto& textureManager = renderer->GetTextureManager();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, textureManager.GetTexture(TextureManager::Texture::ClipDepth)),
			nvrhi::BindingSetItem::Texture_SRV(2, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(3, giRes->prevGBufferDepth),
			nvrhi::BindingSetItem::Texture_SRV(4, giRes->prevGBufferNormals),
			nvrhi::BindingSetItem::Texture_SRV(5, giRes->secondaryGBufferPositionNormal),
			nvrhi::BindingSetItem::Texture_SRV(6, giRes->secondaryGBufferRadiance),
			nvrhi::BindingSetItem::Texture_SRV(7, giRes->secondaryGBufferDiffuseAlbedo),
			nvrhi::BindingSetItem::Texture_SRV(8, giRes->secondaryGBufferSpecularF0Roughness),
			nvrhi::BindingSetItem::TypedBuffer_SRV(9, giRes->neighborOffsetBuffer),
			nvrhi::BindingSetItem::Texture_SRV(10, textureManager.GetTexture(TextureManager::Texture::MotionVectors3D)),
			nvrhi::BindingSetItem::Texture_SRV(11, rrInput->diffuseAlbedo),
			nvrhi::BindingSetItem::Texture_SRV(12, rrInput->specularAlbedo),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(13, giRes->surfaceDataBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, giRes->reservoirBuffer),
			nvrhi::BindingSetItem::Texture_UAV(1, renderer->GetMainTexture()),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
		};

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_DirtyBindings = false;
	}

	void ReSTIRGIPass::FillConstantBuffer(nvrhi::ICommandList* commandList)
	{
		m_Context->SetFrameIndex(m_Context->GetFrameIndex() + 1);

		ReSTIRGIData cbData;
		cbData.giParams.reservoirBufferParams = m_Context->GetReservoirBufferParameters();
		cbData.giParams.bufferIndices = m_Context->GetBufferIndices();
		cbData.giParams.temporalResamplingParams = m_Context->GetTemporalResamplingParameters();
		cbData.giParams.boilingFilterParams = m_Context->GetBoilingFilterParameters();
		cbData.giParams.spatialResamplingParams = m_Context->GetSpatialResamplingParameters();
		cbData.giParams.spatioTemporalResamplingParams = m_Context->GetSpatioTemporalResamplingParameters();
		cbData.giParams.finalShadingParams = m_Context->GetFinalShadingParameters();

		// Fill runtime parameters
		cbData.runtimeParams.neighborOffsetMask = 8191; // 8192 - 1
		cbData.runtimeParams.activeCheckerboardField = 0; // No checkerboard
		cbData.runtimeParams.frameIndex = m_Context->GetFrameIndex();
		cbData.runtimeParams.pad2 = 0;

		// Set uniform random number for permutation sampling
		auto& tparams = cbData.giParams.temporalResamplingParams;
		tparams.uniformRandomNumber = m_Context->GetFrameIndex();

		commandList->writeBuffer(m_ConstantBuffer, &cbData, sizeof(cbData));
	}

	void ReSTIRGIPass::CopyCurrentGBufferToPrevious(nvrhi::ICommandList* commandList)
	{
		auto* renderer = GetRenderer();
		auto* giRes = renderer->GetReSTIRGIResources();
		auto* renderTargets = renderer->GetRenderTargets();

		auto& textureManager = renderer->GetTextureManager();

		// Copy current depth to previous
		commandList->copyTexture(giRes->prevGBufferDepth, nvrhi::TextureSlice(), textureManager.GetTexture(TextureManager::Texture::ClipDepth), nvrhi::TextureSlice());

		// Copy current normals to previous (from PathTracing UAV3 output)
		commandList->copyTexture(giRes->prevGBufferNormals, nvrhi::TextureSlice(), renderTargets->normalRoughness, nvrhi::TextureSlice());
	}

	void ReSTIRGIPass::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		if (m_ResamplingMode == rtxdi::ReSTIRGI_ResamplingMode::None)
			return;

		// Deferred neighbor offset upload (must happen on an open command list)
		auto* giRes = GetRenderer()->GetReSTIRGIResources();
		if (giRes->needsNeighborOffsetUpload)
		{
			commandList->beginTrackingBufferState(giRes->neighborOffsetBuffer, nvrhi::ResourceStates::Common);
			commandList->writeBuffer(giRes->neighborOffsetBuffer, giRes->neighborOffsetData.data(), giRes->neighborOffsetData.size());
			commandList->setPermanentBufferState(giRes->neighborOffsetBuffer, nvrhi::ResourceStates::ShaderResource);
			giRes->needsNeighborOffsetUpload = false;
			giRes->neighborOffsetData.clear();
			giRes->neighborOffsetData.shrink_to_fit();
		}

		CheckBindings();

		if (!m_BindingSet)
			return;

		FillConstantBuffer(commandList);

		auto resolution = GetRenderer()->GetDynamicResolution();
		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);

		nvrhi::BindingSetVector bindings = { m_BindingSet };

		switch (m_ResamplingMode)
		{
		case rtxdi::ReSTIRGI_ResamplingMode::Temporal:
		{
			if (m_TemporalPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_TemporalPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}
			break;
		}

		case rtxdi::ReSTIRGI_ResamplingMode::Spatial:
		{
			// Spatial only: still need to create initial reservoirs,
			// so run temporal (which creates them) then spatial
			if (m_TemporalPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_TemporalPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}
			if (m_SpatialPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_SpatialPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}
			break;
		}

		case rtxdi::ReSTIRGI_ResamplingMode::TemporalAndSpatial:
		{
			if (m_TemporalPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_TemporalPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}
			if (m_SpatialPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_SpatialPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}
			break;
		}

		case rtxdi::ReSTIRGI_ResamplingMode::FusedSpatiotemporal:
		{
			if (m_FusedPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_FusedPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}
			break;
		}
		}

		// Final shading pass
		if (m_FinalShadingPipeline) {
			nvrhi::ComputeState state;
			state.pipeline = m_FinalShadingPipeline;
			state.bindings = bindings;
			commandList->setComputeState(state);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}

		// Copy current G-buffer data to previous frame buffers for next frame's temporal resampling
		CopyCurrentGBufferToPrevious(commandList);
	}
}
