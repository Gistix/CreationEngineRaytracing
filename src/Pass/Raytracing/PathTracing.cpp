#include "PathTracing.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	PathTracing::PathTracing(Renderer* renderer, SceneTLAS* sceneTLAS, LightTLAS* lightTLAS, SHaRC* sharc)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_LightTLAS(lightTLAS), m_SHaRC(sharc)
	{
		m_LinearWrapSampler = GetRenderer()->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		CreatePipeline();
	}

	void PathTracing::CreatePipeline()
	{
		CreateRootSignature();

		auto* scene = Scene::GetSingleton();
		auto& rtSettings = scene->m_Settings.RaytracingSettings;

		const auto bouncesWStr = std::to_wstring(rtSettings.Bounces);
		const auto samplesWStr = std::to_wstring(rtSettings.SamplesPerPixel);

		eastl::vector<DxcDefine> defines = {
			{ L"MAX_BOUNCES", bouncesWStr.c_str() },
			{ L"MAX_SAMPLES", samplesWStr.c_str() },
			{ L"USE_LIGHT_TLAS", L"0" },
			{ L"SHARC", L"" },
			{ L"SHARC_UPDATE", L"0" },
			{ L"SHARC_RESOLVE", L"0" },
			{ L"SHARC_DEBUG", L"0" },
			{ L"DEBUG_TRACE_HEATMAP", L"0" },
			{ L"EXP_VANILLA_PBR_ROUGHNESS", L"0" },
			{ L"EXP_VANILLA_PBR_METALLIC", L"0" }
		};

		if (GetRenderer()->m_Settings.UseRayQuery)
		{
			if (!CreateComputePipeline(defines))
				return;
		}
		else
		{
			if (!CreateRayTracingPipeline(defines))
				return;
		}
	}

	void PathTracing::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_DirtyBindings = true;
	}

	void PathTracing::CreateRootSignature()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3),
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6),
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Texture_UAV(0),
			nvrhi::BindingLayoutItem::Texture_UAV(1),
			nvrhi::BindingLayoutItem::Texture_UAV(2),
			nvrhi::BindingLayoutItem::Texture_UAV(3),
			nvrhi::BindingLayoutItem::Texture_UAV(4)
		};

#if defined(NVAPI)
		globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::TypedBuffer_UAV(127));
#endif

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	bool PathTracing::CreateRayTracingPipeline(eastl::vector<DxcDefine>& defines)
	{
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		defines.emplace_back(L"USE_RAY_QUERY", L"0");

		auto device = GetRenderer()->GetDevice();

		// Compile Libraries
		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines);
		auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/Miss.hlsl", defines);
		auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/ClosestHit.hlsl", defines);
		auto anyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/AnyHit.hlsl", defines);

		nvrhi::rt::PipelineDesc pipelineDesc;

		// Pipeline Shaders
		pipelineDesc.shaders = {
			{ "RayGen", rayGenLib->getShader("Main", nvrhi::ShaderType::RayGeneration), nullptr },
			{ "Miss", missLib->getShader("Main", nvrhi::ShaderType::Miss), nullptr }
		};

		pipelineDesc.hitGroups = {
			{
				"HitGroup",
				hitLib->getShader("Main", nvrhi::ShaderType::ClosestHit),
				anyHitLib->getShader("Main", nvrhi::ShaderType::AnyHit),
				nullptr,  // intersection
				nullptr,  // binding layout
				false     // isProceduralPrimitive
			}
		};

		pipelineDesc.globalBindingLayouts = {
			m_BindingLayout,
			sceneGraph->GetTriangleDescriptors()->m_Layout,
			sceneGraph->GetVertexDescriptors()->m_Layout,
			sceneGraph->GetTextureDescriptors()->m_Layout,
			m_LightTLAS->GetBindlessLayout()
		};

		pipelineDesc.maxPayloadSize = 20;
		pipelineDesc.allowOpacityMicromaps = true;

#if defined(NVAPI)
		pipelineDesc.hlslExtensionsUAV = 127;
#endif

		m_RayPipeline = device->createRayTracingPipeline(pipelineDesc);
		if (!m_RayPipeline)
			return false;

		auto shaderTableDesc = nvrhi::rt::ShaderTableDesc()
			.enableCaching(3)
			.setDebugName("Shader Table");

		m_ShaderTable = m_RayPipeline->createShaderTable(shaderTableDesc);
		if (!m_ShaderTable)
			return false;

		m_ShaderTable->setRayGenerationShader("RayGen");
		m_ShaderTable->addMissShader("Miss");
		m_ShaderTable->addHitGroup("HitGroup");

		return true;
	}

	bool PathTracing::CreateComputePipeline(eastl::vector<DxcDefine>& defines)
	{
		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		if (!m_ComputeShader)
			return false;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
			.addBindingLayout(m_LightTLAS->GetBindlessLayout());

		m_ComputePipeline = GetRenderer()->GetDevice()->createComputePipeline(pipelineDesc);

		if (!m_ComputePipeline)
			return false;

		return true;
	}

	void PathTracing::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

		auto* rrInput = renderer->GetRRInput();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(3, m_SHaRC->GetSHaRCConstantBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS()),
			nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, m_SHaRC->GetResolveBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(6, m_SHaRC->GetHashEntriesBuffer()),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture()),
			nvrhi::BindingSetItem::Texture_UAV(1, rrInput->diffuseAlbedo),
			nvrhi::BindingSetItem::Texture_UAV(2, rrInput->specularAlbedo),
			nvrhi::BindingSetItem::Texture_UAV(3, rrInput->normalRoughness),
			nvrhi::BindingSetItem::Texture_UAV(4, rrInput->specularHitDistance)
		};

		
#if defined(NVAPI)
		bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_UAV(127, nullptr));
#endif

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void PathTracing::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			m_LightTLAS->GetLightDescriptorTable()
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

			auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 32);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}
	}
}