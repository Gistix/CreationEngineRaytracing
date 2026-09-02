#include "IndirectLighting.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"

namespace Pass::Raytracing::PathTracing
{
	IndirectLighting::IndirectLighting(Renderer* renderer, SceneTLAS* sceneTLAS)
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

	void IndirectLighting::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void IndirectLighting::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
	}

	void IndirectLighting::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetPathTracingDefines(settings, false, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreateBindingLayout();
			CreatePipeline();
			m_BindingSetDirty.fill(true);
		}
	}

	void IndirectLighting::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = GetRenderer()->m_Settings.UseRayQuery ? nvrhi::ShaderType::Compute : nvrhi::ShaderType::AllRayTracing;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::Sampler(2),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),           // SkyHemisphere
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2), // Lights
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3), // Instances
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4), // Meshes
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5), // Transforms
			nvrhi::BindingLayoutItem::RawBuffer_SRV(6),        // MeshSlotRemap
			nvrhi::BindingLayoutItem::RawBuffer_SRV(7),        // PropertiesBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(8),          // Depth
			nvrhi::BindingLayoutItem::Texture_SRV(9),          // Albedo
			nvrhi::BindingLayoutItem::Texture_SRV(10),         // EmissiveMetallic
			nvrhi::BindingLayoutItem::Texture_SRV(11),         // NormalRoughness
			nvrhi::BindingLayoutItem::Texture_SRV(12),         // Material
			nvrhi::BindingLayoutItem::Texture_SRV(13),         // WaterFlowMap
			nvrhi::BindingLayoutItem::Texture_SRV(14),         // ProjNoiseMap
			nvrhi::BindingLayoutItem::Texture_SRV(15),         // SkinDetailNormal
			nvrhi::BindingLayoutItem::Texture_SRV(16),         // WaterDisplacementMap
			nvrhi::BindingLayoutItem::Texture_UAV(0)           // Output
		};

#if defined(NVAPI)
		if (!GetRenderer()->m_Settings.UseRayQuery)
			globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::TypedBuffer_UAV(127));
#endif

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	bool IndirectLighting::CreateRayTracingPipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"0");
		eastl::vector<DxcDefine> commonDefines;

		auto device = GetRenderer()->GetDevice();

		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/IndirectLighting/RayGeneration.hlsl", defines);
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

		pipelineDesc.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetPrevPositionDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout);

		pipelineDesc.maxPayloadSize = 20;
		pipelineDesc.allowOpacityMicromaps = false;

#if defined(NVAPI)
		pipelineDesc.hlslExtensionsUAV = 127;
#endif

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

	bool IndirectLighting::CreateComputePipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto device = GetRenderer()->GetDevice();

		auto rayGenBlob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/IndirectLighting/RayGeneration.hlsl", defines, ShaderStage::Compute);
		if (!rayGenBlob)
			return false;

		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());
		if (!m_ComputeShader)
			return false;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetPrevPositionDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
		if (!m_ComputePipeline)
			return false;

		return true;
	}

	void IndirectLighting::CreatePipeline()
	{
		if (GetRenderer()->m_Settings.UseRayQuery) {
			CreateComputePipeline();
		} else {
			CreateRayTracingPipeline();
		}
	}

	void IndirectLighting::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();
		auto& textureManager = renderer->RenderTargetManager();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Sampler(1, m_LinearClampSampler),
			nvrhi::BindingSetItem::Sampler(2, m_PointWrapSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetTransformBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(6, sceneGraph->GetMeshSlotRemapBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(7, sceneGraph->GetPropertiesBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(8, textureManager.GetTexture(RenderTarget::ClipDepth)),
			nvrhi::BindingSetItem::Texture_SRV(9, textureManager.GetTexture(RenderTarget::Albedo)),
			nvrhi::BindingSetItem::Texture_SRV(10, textureManager.GetTexture(RenderTarget::EmissiveMetallic)),
			nvrhi::BindingSetItem::Texture_SRV(11, textureManager.GetTexture(RenderTarget::NormalRoughness)),
			nvrhi::BindingSetItem::Texture_SRV(12, textureManager.GetTexture(RenderTarget::Material)),
			nvrhi::BindingSetItem::Texture_SRV(13, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::Texture_SRV(14, scene->GetProjNoiseTexture()),
			nvrhi::BindingSetItem::Texture_SRV(15, scene->GetSkinDetailNormalTexture()),
			nvrhi::BindingSetItem::Texture_SRV(16, renderer->GetWaterDisplacementTexture()),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

#if defined(NVAPI)
		if (!renderer->m_Settings.UseRayQuery)
			bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_UAV(127, nullptr));
#endif

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void IndirectLighting::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSets[currentSlot],
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetMaterialDescriptors()->m_DescriptorTable,
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
			sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
		};

		auto resolution = Renderer::GetSingleton()->GetDynamicResolution();

		if (m_RayPipeline)
		{
			nvrhi::rt::State state;
			state.shaderTable = m_ShaderTable;
			state.bindings = bindings;
			commandList->setRayTracingState(state);

			nvrhi::rt::DispatchRaysArguments args;
			args.width = resolution.x;
			args.height = resolution.y;
			commandList->dispatchRays(args);
		}
		else if (m_ComputePipeline)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_ComputePipeline;
			state.bindings = bindings;
			commandList->setComputeState(state);

			auto threadGroupSize = Util::Math::GetDispatchCount(resolution, Constants::PT_DISPATCH_THREADS);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}
	}
}
