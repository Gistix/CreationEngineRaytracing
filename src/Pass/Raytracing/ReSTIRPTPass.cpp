#include "ReSTIRPTPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "Constants.h"

#include "ReSTIRPTData.hlsli"
#include "Utils/Shader.h"
#include "ShaderUtils.h"

namespace Pass::Raytracing
{
	ReSTIRPTPass::ReSTIRPTPass(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		m_Defines = Util::Shader::GetRaytracingDefines(Scene::GetSingleton()->m_Settings, false, false);

		auto resolution = renderer->GetResolution();

		rtxdi::ReSTIRPTStaticParameters staticParams;
		staticParams.RenderWidth = resolution.x;
		staticParams.RenderHeight = resolution.y;
		staticParams.CheckerboardSamplingMode = rtxdi::CheckerboardMode::Off;

		m_Context = eastl::make_unique<rtxdi::ReSTIRPTContext>(staticParams);

		m_LinearWrapSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_LinearClampSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		m_PointWrapSampler = renderer->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(false));

		m_ConstantBuffer = renderer->GetDevice()->createBuffer(
			nvrhi::utils::CreateVolatileConstantBufferDesc(
				sizeof(ReSTIRPTData), "ReSTIR PT Data", Constants::MAX_CB_VERSIONS));

		CreateBindingLayout();
		CreatePipeline();
	}

	void ReSTIRPTPass::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc desc;
		desc.visibility = nvrhi::ShaderType::Compute;
		desc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),    // b0: CameraData
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),    // b1: ReSTIRPTData
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),    // b2: FeatureData
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3),    // b3: RaytracingData
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),     // t0: SceneBVH
			nvrhi::BindingLayoutItem::Texture_SRV(1),               // t1: SkyHemisphere
			nvrhi::BindingLayoutItem::Texture_SRV(2),               // t2: WaterFlowMap
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),      // t3: Lights
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),      // t4: Instances
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),      // t5: Meshes
			nvrhi::BindingLayoutItem::Texture_SRV(9),               // t9: SkinDetailNormal
			nvrhi::BindingLayoutItem::Texture_SRV(10),              // t10: CurrentDepth
			nvrhi::BindingLayoutItem::Texture_SRV(11),              // t11: CurrentNormals
			nvrhi::BindingLayoutItem::Texture_SRV(12),              // t12: PreviousDepth
			nvrhi::BindingLayoutItem::Texture_SRV(13),              // t13: PreviousNormals
			nvrhi::BindingLayoutItem::TypedBuffer_SRV(14),          // t14: NeighborOffsets
			nvrhi::BindingLayoutItem::Texture_SRV(15),              // t15: MotionVectors
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(16),     // t16: SurfaceDataBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(17),              // t17: PrimaryDiffuseAlbedo
			nvrhi::BindingLayoutItem::Texture_SRV(18),              // t18: PrimarySpecularAlbedo
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),      // u0: PTReservoirs
			nvrhi::BindingLayoutItem::Texture_UAV(1),               // u1: OutputRadiance
			nvrhi::BindingLayoutItem::Sampler(0),                   // s0
			nvrhi::BindingLayoutItem::Sampler(1),                   // s1
			nvrhi::BindingLayoutItem::Sampler(2),                   // s2
		};
		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(desc);
	}

	void ReSTIRPTPass::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(DxcDefine{ L"USE_RAY_QUERY", L"1" });

		auto createPipeline = [&](const wchar_t* shaderPath, const char* stageName, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			shader = nullptr;
			pipeline = nullptr;

			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, shaderPath, defines, L"cs_6_5");
			if (!blob) {
				logger::error("ReSTIRPTPass::CreatePipeline - Failed to compile {} shader.", stageName);
				return;
			}

			shader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!shader) {
				logger::error("ReSTIRPTPass::CreatePipeline - Failed to create {} shader.", stageName);
				return;
			}

			pipeline = device->createComputePipeline(
				nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout)
				.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetPrevPositionDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout));
			if (!pipeline) {
				logger::error("ReSTIRPTPass::CreatePipeline - Failed to create {} pipeline.", stageName);
			}
		};

		createPipeline(L"data/shaders/raytracing/RTXDI/ReSTIRPT/PTInitialSampling.hlsl", "initial sampling", m_InitialSamplingShader, m_InitialSamplingPipeline);
		createPipeline(L"data/shaders/raytracing/RTXDI/ReSTIRPT/PTTemporalResampling.hlsl", "temporal resampling", m_TemporalShader, m_TemporalPipeline);
		createPipeline(L"data/shaders/raytracing/RTXDI/ReSTIRPT/PTSpatialResampling.hlsl", "spatial resampling", m_SpatialShader, m_SpatialPipeline);
		createPipeline(L"data/shaders/raytracing/RTXDI/ReSTIRPT/PTFinalShading.hlsl", "final shading", m_FinalShadingShader, m_FinalShadingPipeline);
	}

	void ReSTIRPTPass::ResolutionChanged(uint2 resolution)
	{
		rtxdi::ReSTIRPTStaticParameters staticParams;
		staticParams.RenderWidth = resolution.x;
		staticParams.RenderHeight = resolution.y;
		staticParams.CheckerboardSamplingMode = rtxdi::CheckerboardMode::Off;

		m_Context = eastl::make_unique<rtxdi::ReSTIRPTContext>(staticParams);
		m_DirtyBindings = true;
	}

	void ReSTIRPTPass::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetRaytracingDefines(settings, false, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_DirtyBindings = true;
		}

		m_Enabled = settings.ReSTIRPT.Enabled;

		if (!m_Enabled)
			return;

		m_ResamplingMode = static_cast<rtxdi::ReSTIRPT_ResamplingMode>(
			static_cast<uint32_t>(settings.ReSTIRPT.ResamplingMode));
		m_Context->SetResamplingMode(m_ResamplingMode);

		// Initial sampling parameters
		{
			RTXDI_PTInitialSamplingParameters iparams = rtxdi::GetDefaultReSTIRPTInitialSamplingParams();
			// RTXDI counts bounce depth as path vertices: 0=camera, 1=primary hit,
			// 2=secondary hit. Match the user-facing PT bounce count used by Reference.
			iparams.maxBounceDepth = settings.ReSTIRPT.MaxBounceDepth + 1;
			iparams.maxRcVertexLength = settings.ReSTIRPT.MaxRcVertexLength;
			m_Context->SetInitialSamplingParameters(iparams);
		}

		// Reconnection parameters
		{
			RTXDI_PTReconnectionParameters rparams = rtxdi::GetDefaultReSTIRPTReconnectionParameters();
			rparams.roughnessThreshold = settings.ReSTIRPT.RoughnessThreshold;
			rparams.distanceThreshold = settings.ReSTIRPT.DistanceThreshold;
			rparams.minConnectionFootprint = settings.ReSTIRPT.MinConnectionFootprint;
			rparams.reconnectionMode = settings.ReSTIRPT.UseFootprintMode
				? RTXDI_PTReconnectionMode::Footprint
				: RTXDI_PTReconnectionMode::FixedThreshold;
			m_Context->SetReconnectionParameters(rparams);
		}

		// Hybrid shift parameters
		{
			RTXDI_PTHybridShiftPerFrameParameters hparams = rtxdi::GetDefaultReSTIRPTHybridShiftParams();
			hparams.maxBounceDepth = settings.ReSTIRPT.MaxBounceDepth + 1;
			hparams.maxRcVertexLength = settings.ReSTIRPT.MaxRcVertexLength;
			m_Context->SetHybridShiftParameters(hparams);
		}

		// Temporal parameters
		{
			RTXDI_PTTemporalResamplingParameters tparams = rtxdi::GetDefaultReSTIRPTTemporalResamplingParams();
			tparams.depthThreshold = settings.ReSTIRPT.TemporalDepthThreshold;
			tparams.normalThreshold = settings.ReSTIRPT.TemporalNormalThreshold;
			tparams.maxHistoryLength = settings.ReSTIRPT.MaxHistoryLength;
			tparams.maxReservoirAge = settings.ReSTIRPT.MaxReservoirAge;
			tparams.enablePermutationSampling = settings.ReSTIRPT.EnablePermutationSampling ? 1 : 0;
			tparams.enableFallbackSampling = settings.ReSTIRPT.EnableFallbackSampling ? 1 : 0;
			tparams.enableVisibilityBeforeCombine = settings.ReSTIRPT.EnableTemporalVisibility ? 1 : 0;
			m_Context->SetTemporalResamplingParameters(tparams);
		}

		// Spatial parameters
		{
			RTXDI_PTSpatialResamplingParameters sparams = rtxdi::GetDefaultReSTIRPTSpatialResamplingParams();
			sparams.numSpatialSamples = settings.ReSTIRPT.SpatialNumSamples;
			sparams.numDisocclusionBoostSamples = settings.ReSTIRPT.SpatialDisocclusionBoostSamples;
			sparams.samplingRadius = settings.ReSTIRPT.SpatialSamplingRadius;
			sparams.depthThreshold = settings.ReSTIRPT.SpatialDepthThreshold;
			sparams.normalThreshold = settings.ReSTIRPT.SpatialNormalThreshold;
			m_Context->SetSpatialResamplingParameters(sparams);
		}

		// Boiling filter
		{
			RTXDI_BoilingFilterParameters bparams = rtxdi::GetDefaultReSTIRPTBoilingFilterParams();
			bparams.enableBoilingFilter = settings.ReSTIRPT.EnableBoilingFilter ? 1 : 0;
			bparams.boilingFilterStrength = settings.ReSTIRPT.BoilingFilterStrength;
			m_Context->SetBoilingFilterParameters(bparams);
		}
	}

	void ReSTIRPTPass::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();
		auto* ptRes = renderer->GetReSTIRPTResources();
		auto* renderTargets = renderer->GetRenderTargets();

		auto& textureManager = renderer->RenderTargetManager();

		// Material textures may not be available (only exist under NRD/DLSS_RR). Use black as fallback.
		auto diffuseAlbedoTex = textureManager.GetTexture(RenderTarget::DiffuseAlbedo);
		auto specularAlbedoTex = textureManager.GetTexture(RenderTarget::RRSpecularAlbedo);
		if (!diffuseAlbedoTex) diffuseAlbedoTex = renderer->GetBlackDescriptor();
		if (!specularAlbedoTex) specularAlbedoTex = renderer->GetBlackDescriptor();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(3, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(9, scene->GetSkinDetailNormalTexture()),
			nvrhi::BindingSetItem::Texture_SRV(10, textureManager.GetTexture(RenderTarget::ClipDepth)),
			nvrhi::BindingSetItem::Texture_SRV(11, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(12, ptRes->prevGBufferDepth),
			nvrhi::BindingSetItem::Texture_SRV(13, ptRes->prevGBufferNormals),
			nvrhi::BindingSetItem::TypedBuffer_SRV(14, ptRes->neighborOffsetBuffer),
			nvrhi::BindingSetItem::Texture_SRV(15, textureManager.GetTexture(RenderTarget::MotionVectors3D)),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(16, ptRes->surfaceDataBuffer),
			nvrhi::BindingSetItem::Texture_SRV(17, diffuseAlbedoTex),
			nvrhi::BindingSetItem::Texture_SRV(18, specularAlbedoTex),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, ptRes->reservoirBuffer),
			nvrhi::BindingSetItem::Texture_UAV(1, renderer->GetMainTexture()),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Sampler(1, m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(2, m_PointWrapSampler),
		};

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_DirtyBindings = false;
	}

	void ReSTIRPTPass::FillConstantBuffer(nvrhi::ICommandList* commandList)
	{
		m_Context->SetFrameIndex(m_Context->GetFrameIndex() + 1);

		ReSTIRPTData cbData;
		cbData.ptParams.reservoirBuffer = m_Context->GetReservoirBufferParameters();
		cbData.ptParams.bufferIndices = m_Context->GetBufferIndices();
		cbData.ptParams.initialSampling = m_Context->GetInitialSamplingParameters();
		cbData.ptParams.reconnection = m_Context->GetReconnectionParameters();
		cbData.ptParams.temporalResampling = m_Context->GetTemporalResamplingParameters();
		cbData.ptParams.hybridShift = m_Context->GetHybridShiftParameters();
		cbData.ptParams.boilingFilter = m_Context->GetBoilingFilterParameters();
		cbData.ptParams.spatialResampling = m_Context->GetSpatialResamplingParameters();

		// Fill runtime parameters
		cbData.runtimeParams.neighborOffsetMask = 8191; // 8192 - 1
		cbData.runtimeParams.activeCheckerboardField = 0; // No checkerboard
		cbData.runtimeParams.frameIndex = m_Context->GetFrameIndex();
		cbData.runtimeParams.pad2 = 0;
		cbData.renderSize = GetRenderer()->GetDynamicResolution();
		cbData.pad0 = 0;
		cbData.pad1 = 0;

		commandList->writeBuffer(m_ConstantBuffer, &cbData, sizeof(cbData));
	}

	void ReSTIRPTPass::DispatchInitialSampling(nvrhi::ICommandList* commandList, const nvrhi::BindingSetVector& bindings, uint2 threadGroupSize)
	{
		if (!m_InitialSamplingPipeline)
			return;

		nvrhi::ComputeState state;
		state.pipeline = m_InitialSamplingPipeline;
		state.bindings = bindings;
		commandList->setComputeState(state);
		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}

	void ReSTIRPTPass::CopyCurrentGBufferToPrevious(nvrhi::ICommandList* commandList)
	{
		auto* renderer = GetRenderer();
		auto* ptRes = renderer->GetReSTIRPTResources();

		auto& textureManager = renderer->RenderTargetManager();

		// Copy current depth to previous
		commandList->copyTexture(ptRes->prevGBufferDepth, nvrhi::TextureSlice(), textureManager.GetTexture(RenderTarget::ClipDepth), nvrhi::TextureSlice());

		// Copy current normals to previous
		commandList->copyTexture(ptRes->prevGBufferNormals, nvrhi::TextureSlice(), renderer->GetRenderTargets()->normalRoughness, nvrhi::TextureSlice());
	}

	void ReSTIRPTPass::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		if (m_ResamplingMode == rtxdi::ReSTIRPT_ResamplingMode::None)
			return;

		// Deferred neighbor offset upload
		auto* ptRes = GetRenderer()->GetReSTIRPTResources();
		if (ptRes->needsNeighborOffsetUpload)
		{
			commandList->beginTrackingBufferState(ptRes->neighborOffsetBuffer, nvrhi::ResourceStates::Common);
			commandList->writeBuffer(ptRes->neighborOffsetBuffer, ptRes->neighborOffsetData.data(), ptRes->neighborOffsetData.size());
			commandList->setPermanentBufferState(ptRes->neighborOffsetBuffer, nvrhi::ResourceStates::ShaderResource);
			ptRes->needsNeighborOffsetUpload = false;
			ptRes->neighborOffsetData.clear();
			ptRes->neighborOffsetData.shrink_to_fit();
		}

		CheckBindings();

		if (!m_BindingSet)
			return;

		FillConstantBuffer(commandList);

		auto resolution = GetRenderer()->GetDynamicResolution();
		auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 8);

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetMaterialDescriptors()->m_DescriptorTable,
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
			sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
		};

		DispatchInitialSampling(commandList, bindings, threadGroupSize);

		// Temporal resampling
		if (m_ResamplingMode == rtxdi::ReSTIRPT_ResamplingMode::Temporal ||
			m_ResamplingMode == rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial)
		{
			if (m_TemporalPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_TemporalPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			}
		}

		// Spatial resampling
		if (m_ResamplingMode == rtxdi::ReSTIRPT_ResamplingMode::Spatial ||
			m_ResamplingMode == rtxdi::ReSTIRPT_ResamplingMode::TemporalAndSpatial)
		{
			if (m_SpatialPipeline) {
				nvrhi::ComputeState state;
				state.pipeline = m_SpatialPipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);
				commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
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

		// Copy current G-buffer data to previous frame buffers
		CopyCurrentGBufferToPrevious(commandList);
	}
}
