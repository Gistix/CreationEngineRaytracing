#include "ReorderedIndirectLighting.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"

namespace Pass::Raytracing::PathTracing
{
	struct GpuRayRecord
	{
		float Origin[3];
		uint32_t PixelCoord;
		float Direction[3];
		uint32_t RandomSeed;
		float Throughput[3];
		float Pad;
	};

	ReorderedIndirectLighting::ReorderedIndirectLighting(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		auto device = GetRenderer()->GetDevice();

		m_LinearWrapSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_LinearClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		m_PointWrapSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(false));

		const auto& settings = Scene::GetSingleton()->m_Settings;
		m_Defines = Util::Shader::GetPathTracingDefines(settings, false, false);

		m_SceneTLAS->GetTopLevelAS().AddListener(this);
	}

	void ReorderedIndirectLighting::Initialize()
	{
		CreateBindingLayouts();
		CreatePipeline();
	}

	void ReorderedIndirectLighting::AllocateBuffers(uint2 resolution)
	{
		if (resolution.x == 0 || resolution.y == 0)
			return;

		if (m_AllocatedResolution == resolution && m_RayRecordBuffer)
			return;

		m_AllocatedResolution = resolution;
		auto* device = GetRenderer()->GetDevice();
		uint32_t pixelCount = resolution.x * resolution.y;

		m_RayRecordBuffer = Util::CreateStructuredBuffer<GpuRayRecord>(device, pixelCount, "Indirect RayRecord Buffer", true);
		m_RayKeyBuffer = Util::CreateStructuredBuffer<uint32_t>(device, pixelCount, "Indirect RayKey Buffer", true);
		m_SortedRayRecordBuffer = Util::CreateStructuredBuffer<GpuRayRecord>(device, pixelCount, "Indirect SortedRayRecord Buffer", true);

		if (!m_CounterBuffer) {
			m_CounterBuffer = Util::CreateStructuredBuffer<uint32_t>(device, 1, "Indirect Counter Buffer", true);
			m_BinHistogramBuffer = Util::CreateStructuredBuffer<uint32_t>(device, kNumBins, "Indirect BinHistogram Buffer", true);
			m_BinOffsetBuffer = Util::CreateStructuredBuffer<uint32_t>(device, kNumBins, "Indirect BinOffset Buffer", true);
			m_BinCounterBuffer = Util::CreateStructuredBuffer<uint32_t>(device, kNumBins, "Indirect BinCounter Buffer", true);
		}

		m_GenerateBindingSetDirty.fill(true);
		m_TraceBindingSetDirty.fill(true);
		m_ReorderBindingSetsDirty = true;
	}

	void ReorderedIndirectLighting::ResolutionChanged(uint2 resolution)
	{
		AllocateBuffers(resolution);
	}

	void ReorderedIndirectLighting::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetPathTracingDefines(settings, false, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreateBindingLayouts();
			CreatePipeline();
			m_GenerateBindingSetDirty.fill(true);
			m_TraceBindingSetDirty.fill(true);
			m_ReorderBindingSetsDirty = true;
		}
	}

	void ReorderedIndirectLighting::CreateBindingLayouts()
	{
		auto* device = GetRenderer()->GetDevice();

		// 1. Generate Binding Layout
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::Sampler(0),
				nvrhi::BindingLayoutItem::Sampler(1),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
				nvrhi::BindingLayoutItem::Texture_SRV(0),          // Depth
				nvrhi::BindingLayoutItem::Texture_SRV(1),          // Albedo
				nvrhi::BindingLayoutItem::Texture_SRV(2),          // EmissiveMetallic
				nvrhi::BindingLayoutItem::Texture_SRV(3),          // NormalRoughness
				nvrhi::BindingLayoutItem::Texture_SRV(4),          // Material
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0), // RayRecords
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1), // RayKeys
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2), // CounterBuffer
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3)  // BinHistogram
			};
			m_GenerateBindingLayout = device->createBindingLayout(desc);
		}

		// 2. Prefix Sum Binding Layout
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0), // BinHistogram
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1)  // BinOffsets
			};
			m_PrefixSumBindingLayout = device->createBindingLayout(desc);
		}

		// 3. Scatter Binding Layout
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = nvrhi::ShaderType::Compute;
			desc.bindings = {
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0), // CounterBuffer
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1), // BinOffsets
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2), // RayKeys
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3), // RayRecords (unordered)
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0), // SortedRayRecords (sorted)
				nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1)  // BinCounters
			};
			m_ScatterBindingLayout = device->createBindingLayout(desc);
		}

		// 4. Trace Binding Layout
		{
			nvrhi::BindingLayoutDesc desc;
			desc.visibility = GetRenderer()->m_Settings.UseRayQuery ? nvrhi::ShaderType::Compute : nvrhi::ShaderType::AllRayTracing;
			desc.bindings = {
				nvrhi::BindingLayoutItem::Sampler(0),
				nvrhi::BindingLayoutItem::Sampler(1),
				nvrhi::BindingLayoutItem::Sampler(2),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
				nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
				nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
				nvrhi::BindingLayoutItem::Texture_SRV(1),           // SkyHemisphere
				nvrhi::BindingLayoutItem::Texture_SRV(2),           // WaterFlowMap
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3), // Lights
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4), // Instances
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5), // Meshes
				nvrhi::BindingLayoutItem::Texture_SRV(8),          // SkinDetailNormal
				nvrhi::BindingLayoutItem::Texture_SRV(9),          // Water displacement
				nvrhi::BindingLayoutItem::Texture_SRV(10),         // Projection noise
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11), // Transforms
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12), // InstanceLightList
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13), // RayRecords
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(14), // SortedRayIndices
				nvrhi::BindingLayoutItem::StructuredBuffer_SRV(15), // CounterBuffer
				nvrhi::BindingLayoutItem::RawBuffer_SRV(19),        // MeshSlotRemap
				nvrhi::BindingLayoutItem::RawBuffer_SRV(20),        // PropertiesBuffer
				nvrhi::BindingLayoutItem::Texture_UAV(0)            // Output
			};
			m_TraceBindingLayout = device->createBindingLayout(desc);
		}
	}

	bool ReorderedIndirectLighting::CreateGeneratePipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		auto* device = GetRenderer()->GetDevice();

		auto blob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/ReorderedIndirect/GenerateBounce.hlsl", defines, ShaderStage::Compute);
		if (!blob)
			return false;

		m_GenerateShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
		if (!m_GenerateShader)
			return false;

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_GenerateShader)
			.addBindingLayout(m_GenerateBindingLayout);

		m_GeneratePipeline = device->createComputePipeline(pipelineDesc);
		return m_GeneratePipeline != nullptr;
	}

	bool ReorderedIndirectLighting::CreateReorderPipelines()
	{
		eastl::vector<DxcDefine> commonDefines;
		auto* device = GetRenderer()->GetDevice();

		// Prefix Sum
		auto prefixBlob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/ReorderedIndirect/PrefixSumBins.hlsl", commonDefines, ShaderStage::Compute);
		if (!prefixBlob)
			return false;

		m_PrefixSumShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, prefixBlob->GetBufferPointer(), prefixBlob->GetBufferSize());
		if (!m_PrefixSumShader)
			return false;

		auto prefixDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_PrefixSumShader)
			.addBindingLayout(m_PrefixSumBindingLayout);

		m_PrefixSumPipeline = device->createComputePipeline(prefixDesc);
		if (!m_PrefixSumPipeline)
			return false;

		// Scatter
		auto scatterBlob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/ReorderedIndirect/ScatterRays.hlsl", commonDefines, ShaderStage::Compute);
		if (!scatterBlob)
			return false;

		m_ScatterShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, scatterBlob->GetBufferPointer(), scatterBlob->GetBufferSize());
		if (!m_ScatterShader)
			return false;

		auto scatterDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ScatterShader)
			.addBindingLayout(m_ScatterBindingLayout);

		m_ScatterPipeline = device->createComputePipeline(scatterDesc);
		return m_ScatterPipeline != nullptr;
	}

	bool ReorderedIndirectLighting::CreateTracePipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		auto* device = GetRenderer()->GetDevice();

		if (GetRenderer()->m_Settings.UseRayQuery)
		{
			defines.emplace_back(L"USE_RAY_QUERY", L"1");

			auto blob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/ReorderedIndirect/TraceBounce.hlsl", defines, ShaderStage::Compute);
			if (!blob)
				return false;

			m_ComputeTraceShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!m_ComputeTraceShader)
				return false;

			auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
			auto pipelineDesc = nvrhi::ComputePipelineDesc()
				.setComputeShader(m_ComputeTraceShader)
				.addBindingLayout(m_TraceBindingLayout)
				.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetPrevPositionDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout);

			m_ComputeTracePipeline = device->createComputePipeline(pipelineDesc);
			return m_ComputeTracePipeline != nullptr;
		}
		else
		{
			defines.emplace_back(L"USE_RAY_QUERY", L"0");
			eastl::vector<DxcDefine> commonDefines;

			auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/ReorderedIndirect/TraceBounce.hlsl", defines);
			if (!rayGenLib)
				return false;

			auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/Miss.hlsl", commonDefines);
			auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ClosestHit.hlsl", commonDefines);
			auto anyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/AnyHit.hlsl", commonDefines);
			auto shadowMissLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ShadowMiss.hlsl", commonDefines);
			auto shadowAnyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ShadowAnyHit.hlsl", commonDefines);

			nvrhi::rt::PipelineDesc pipelineDesc;
			pipelineDesc.shaders = {
				{ "RayGen", rayGenLib->getShader("Main", nvrhi::ShaderType::RayGeneration), nullptr },
				{ "Miss", missLib->getShader("Main", nvrhi::ShaderType::Miss), nullptr },
				{ "ShadowMiss", shadowMissLib->getShader("Main", nvrhi::ShaderType::Miss), nullptr }
			};

			pipelineDesc.hitGroups = {
				{
					"HitGroup",
					hitLib->getShader("Main", nvrhi::ShaderType::ClosestHit),
					anyHitLib->getShader("Main", nvrhi::ShaderType::AnyHit),
					nullptr, nullptr, false
				},
				{
					"ShadowHitGroup",
					nullptr,
					shadowAnyHitLib->getShader("Main", nvrhi::ShaderType::AnyHit),
					nullptr, nullptr, false
				}
			};

			auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
			pipelineDesc.addBindingLayout(m_TraceBindingLayout)
				.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetPrevPositionDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout);

			pipelineDesc.maxPayloadSize = 20;
			pipelineDesc.allowOpacityMicromaps = false;

			m_RayPipeline = device->createRayTracingPipeline(pipelineDesc);
			if (!m_RayPipeline)
				return false;

			auto shaderTableDesc = nvrhi::rt::ShaderTableDesc()
				.enableCaching(5)
				.setDebugName("Shader Table");

			m_ShaderTable = m_RayPipeline->createShaderTable(shaderTableDesc);
			if (!m_ShaderTable)
				return false;

			m_ShaderTable->setRayGenerationShader("RayGen");
			m_ShaderTable->addMissShader("Miss");
			m_ShaderTable->addMissShader("ShadowMiss");
			m_ShaderTable->addHitGroup("HitGroup");
			m_ShaderTable->addHitGroup("ShadowHitGroup");

			return true;
		}
	}

	void ReorderedIndirectLighting::CreatePipeline()
	{
		CreateGeneratePipeline();
		CreateReorderPipelines();
		CreateTracePipeline();
	}

	void ReorderedIndirectLighting::CheckBindings()
	{
		auto resolution = Renderer::GetSingleton()->GetDynamicResolution();
		if (resolution.x == 0 || resolution.y == 0)
			resolution = Renderer::GetSingleton()->GetResolution();

		if (resolution.x > 0 && resolution.y > 0)
			AllocateBuffers(resolution);

		if (!m_RayRecordBuffer)
			return;

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();
		auto& textureManager = renderer->RenderTargetManager();
		auto* device = renderer->GetDevice();

		// 1. Generate Binding Set
		if (m_GenerateBindingSetDirty[currentSlot] || !m_GenerateBindingSets[currentSlot])
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
				nvrhi::BindingSetItem::Sampler(1, m_LinearClampSampler),
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
				nvrhi::BindingSetItem::Texture_SRV(0, textureManager.GetTexture(RenderTarget::ClipDepth)),
				nvrhi::BindingSetItem::Texture_SRV(1, textureManager.GetTexture(RenderTarget::Albedo)),
				nvrhi::BindingSetItem::Texture_SRV(2, textureManager.GetTexture(RenderTarget::EmissiveMetallic)),
				nvrhi::BindingSetItem::Texture_SRV(3, textureManager.GetTexture(RenderTarget::NormalRoughness)),
				nvrhi::BindingSetItem::Texture_SRV(4, textureManager.GetTexture(RenderTarget::Material)),
				nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_RayRecordBuffer),
				nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_RayKeyBuffer),
				nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_CounterBuffer),
				nvrhi::BindingSetItem::StructuredBuffer_UAV(3, m_BinHistogramBuffer)
			};
			m_GenerateBindingSets[currentSlot] = device->createBindingSet(desc, m_GenerateBindingLayout);
			m_GenerateBindingSetDirty[currentSlot] = false;
		}

		// 2. Reorder Binding Sets (Prefix Sum & Scatter)
		if (m_ReorderBindingSetsDirty || !m_PrefixSumBindingSet || !m_ScatterBindingSet)
		{
			{
				nvrhi::BindingSetDesc desc;
				desc.bindings = {
					nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_BinHistogramBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_BinOffsetBuffer)
				};
				m_PrefixSumBindingSet = device->createBindingSet(desc, m_PrefixSumBindingLayout);
			}
			{
				nvrhi::BindingSetDesc desc;
				desc.bindings = {
					nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_CounterBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_BinOffsetBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_RayKeyBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_SRV(3, m_RayRecordBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_SortedRayRecordBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_BinCounterBuffer)
				};
				m_ScatterBindingSet = device->createBindingSet(desc, m_ScatterBindingLayout);
			}
			m_ReorderBindingSetsDirty = false;
		}

		// 3. Trace Binding Set
		if (m_TraceBindingSetDirty[currentSlot] || !m_TraceBindingSets[currentSlot])
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
				nvrhi::BindingSetItem::Sampler(1, m_LinearClampSampler),
				nvrhi::BindingSetItem::Sampler(2, m_PointWrapSampler),
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
				nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
				nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
				nvrhi::BindingSetItem::Texture_SRV(2, scene->GetFlowMapTexture()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetLightBuffer()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetInstanceBuffer()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetMeshBuffer()),
				nvrhi::BindingSetItem::Texture_SRV(8, scene->GetSkinDetailNormalTexture()),
				nvrhi::BindingSetItem::Texture_SRV(9, renderer->GetWaterDisplacementTexture()),
				nvrhi::BindingSetItem::Texture_SRV(10, scene->GetProjNoiseTexture()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(11, sceneGraph->GetTransformBuffer()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(12, sceneGraph->GetInstanceLightList()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(13, m_SortedRayRecordBuffer),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(14, m_CounterBuffer),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(15, m_CounterBuffer),
				nvrhi::BindingSetItem::RawBuffer_SRV(19, sceneGraph->GetMeshSlotRemapBuffer()),
				nvrhi::BindingSetItem::RawBuffer_SRV(20, sceneGraph->GetPropertiesBuffer()),
				nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
			};
			m_TraceBindingSets[currentSlot] = device->createBindingSet(desc, m_TraceBindingLayout);
			m_TraceBindingSetDirty[currentSlot] = false;
		}
	}

	void ReorderedIndirectLighting::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_GenerateBindingSets[currentSlot] || !m_TraceBindingSets[currentSlot] || !m_PrefixSumBindingSet || !m_ScatterBindingSet)
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
		auto resolution = Renderer::GetSingleton()->GetDynamicResolution();
		if (resolution.x == 0 || resolution.y == 0)
			resolution = Renderer::GetSingleton()->GetResolution();
		uint32_t totalPixels = resolution.x * resolution.y;

		// Clear counters
		commandList->clearBufferUInt(m_CounterBuffer, 0);
		commandList->clearBufferUInt(m_BinHistogramBuffer, 0);
		commandList->clearBufferUInt(m_BinCounterBuffer, 0);
		commandList->commitBarriers();

		// Step 1: Generate Bounce
		{
			nvrhi::ComputeState state;
			state.pipeline = m_GeneratePipeline;
			state.bindings = { m_GenerateBindingSets[currentSlot] };
			commandList->setComputeState(state);

			auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 16.0f);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
			commandList->commitBarriers();
		}

		// Step 2: Prefix Sum Bins (1 threadgroup of 256 threads)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_PrefixSumPipeline;
			state.bindings = { m_PrefixSumBindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(1, 1, 1);
			commandList->commitBarriers();
		}

		// Step 3: Scatter Rays
		{
			nvrhi::ComputeState state;
			state.pipeline = m_ScatterPipeline;
			state.bindings = { m_ScatterBindingSet };
			commandList->setComputeState(state);
			commandList->dispatch(Util::Math::DivideRoundUp(totalPixels, 256u), 1, 1);
			commandList->commitBarriers();
		}

		// Step 4: Trace Bounce
		nvrhi::BindingSetVector traceBindings = {
			m_TraceBindingSets[currentSlot],
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetMaterialDescriptors()->m_DescriptorTable,
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
			sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
		};

		if (m_RayPipeline)
		{
			nvrhi::rt::State state;
			state.shaderTable = m_ShaderTable;
			state.bindings = traceBindings;
			commandList->setRayTracingState(state);

			nvrhi::rt::DispatchRaysArguments args;
			args.width = resolution.x;
			args.height = resolution.y;
			commandList->dispatchRays(args);
		}
		else if (m_ComputeTracePipeline)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_ComputeTracePipeline;
			state.bindings = traceBindings;
			commandList->setComputeState(state);

			commandList->dispatch(Util::Math::DivideRoundUp(totalPixels, 64u), 1, 1);
		}
	}
}
