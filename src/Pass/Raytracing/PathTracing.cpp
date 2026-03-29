#include "PathTracing.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	PathTracing::PathTracing(Renderer* renderer, SceneTLAS* sceneTLAS, SHaRC* sharc)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_SHaRC(sharc)
	{
		m_LinearWrapSampler = GetRenderer()->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_Defines = Util::Shader::GetRaytracingDefines(Scene::GetSingleton()->m_Settings, m_SHaRC != nullptr, false);

		m_SceneTLAS->GetTopLevelAS().AddListener(this);

		CreateBindingLayout();
		CreatePipeline();
	}

	void PathTracing::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_DirtyBindings = true;
	}

	void PathTracing::SettingsChanged(const Settings& settings)
	{
		m_UseStablePlanes = settings.DebugSettings.StablePlanes;

		auto defines = Util::Shader::GetRaytracingDefines(settings, m_SHaRC != nullptr, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_DirtyBindings = true;
		}
	}

	void PathTracing::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = GetRenderer()->m_Settings.UseRayQuery ? nvrhi::ShaderType::Compute : nvrhi::ShaderType::AllRayTracing;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3),
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7),
			nvrhi::BindingLayoutItem::Texture_UAV(0),
			nvrhi::BindingLayoutItem::Texture_UAV(1),
			nvrhi::BindingLayoutItem::Texture_UAV(2),
			nvrhi::BindingLayoutItem::Texture_UAV(3),
			nvrhi::BindingLayoutItem::Texture_UAV(4),
			// Stable Planes UAVs
			nvrhi::BindingLayoutItem::Texture_UAV(5),           // StablePlanesHeader (RWTexture2DArray<uint>)
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6),  // StablePlanesBuffer (RWStructuredBuffer<StablePlane>)
			nvrhi::BindingLayoutItem::Texture_UAV(7),           // StableRadiance (RWTexture2D<float4>)
			// PT Motion Vectors output
			nvrhi::BindingLayoutItem::Texture_UAV(8),            // MotionVectors (RWTexture2D<float4>)
			// PT Depth output
			nvrhi::BindingLayoutItem::Texture_UAV(9),            // Depth (RWTexture2D<float>)
			// ReSTIR GI: Secondary G-Buffer UAVs
			nvrhi::BindingLayoutItem::Texture_UAV(10),           // SecondaryGBufPositionNormal
			nvrhi::BindingLayoutItem::Texture_UAV(11),           // SecondaryGBufRadiance
			nvrhi::BindingLayoutItem::Texture_UAV(12),           // SecondaryGBufDiffuseAlbedo
			nvrhi::BindingLayoutItem::Texture_UAV(13),           // SecondaryGBufSpecularRough
			// ReSTIR GI: Packed primary surface data (ping-pong StructuredBuffer)
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(14)   // SurfaceDataBuffer
		};

#if defined(NVAPI)
		globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::TypedBuffer_UAV(127));
#endif

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void PathTracing::CreatePipeline()
	{
		if (GetRenderer()->m_Settings.UseRayQuery) {
			CreateComputePipeline();
		} else {
			CreateRayTracingPipeline();
		}
	}

	void PathTracing::CreateRayTracingPipelineForMode(int mode, nvrhi::rt::PipelineHandle& outPipeline, nvrhi::rt::ShaderTableHandle& outShaderTable)
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"0");
		defines.emplace_back(L"PATH_TRACER_MODE", mode == 1 ? L"1" : (mode == 2 ? L"2" : L"0"));

		auto device = GetRenderer()->GetDevice();

		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines);
		auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/Miss.hlsl", defines);
		auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ClosestHit.hlsl", defines);
		auto anyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/AnyHit.hlsl", defines);

		nvrhi::rt::PipelineDesc pipelineDesc;
		pipelineDesc.shaders = {
			{ "RayGen", rayGenLib->getShader("Main", nvrhi::ShaderType::RayGeneration), nullptr },
			{ "Miss", missLib->getShader("Main", nvrhi::ShaderType::Miss), nullptr }
		};

		pipelineDesc.hitGroups = {
			{
				"HitGroup",
				hitLib->getShader("Main", nvrhi::ShaderType::ClosestHit),
				anyHitLib->getShader("Main", nvrhi::ShaderType::AnyHit),
				nullptr, nullptr, false
			}
		};

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
		pipelineDesc.globalBindingLayouts = {
			m_BindingLayout,
			sceneGraph->GetTriangleDescriptors()->m_Layout,
			sceneGraph->GetVertexDescriptors()->m_Layout,
			sceneGraph->GetTextureDescriptors()->m_Layout
		};

		pipelineDesc.maxPayloadSize = 20;
		pipelineDesc.allowOpacityMicromaps = true;

#if defined(NVAPI)
		pipelineDesc.hlslExtensionsUAV = 127;
#endif

		outPipeline = device->createRayTracingPipeline(pipelineDesc);
		if (!outPipeline)
			return;

		auto shaderTableDesc = nvrhi::rt::ShaderTableDesc()
			.enableCaching(3)
			.setDebugName("Shader Table");

		outShaderTable = outPipeline->createShaderTable(shaderTableDesc);
		if (!outShaderTable)
			return;

		outShaderTable->setRayGenerationShader("RayGen");
		outShaderTable->addMissShader("Miss");
		outShaderTable->addHitGroup("HitGroup");
	}

	void PathTracing::CreateRayTracingPipeline()
	{
		// Reference mode (mode 0)
		CreateRayTracingPipelineForMode(0, m_RayPipeline, m_ShaderTable);
		// BUILD mode (mode 1)
		CreateRayTracingPipelineForMode(1, m_BuildRayPipeline, m_BuildShaderTable);
		// FILL mode (mode 2)
		CreateRayTracingPipelineForMode(2, m_FillRayPipeline, m_FillShaderTable);
	}

	void PathTracing::CreateComputePipelineForMode(int mode, nvrhi::ShaderHandle& outShader, nvrhi::ComputePipelineHandle& outPipeline)
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"1");
		defines.emplace_back(L"PATH_TRACER_MODE", mode == 1 ? L"1" : (mode == 2 ? L"2" : L"0"));

		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines, L"cs_6_5");
		outShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		if (!outShader)
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(outShader)
			.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout);

		outPipeline = device->createComputePipeline(pipelineDesc);
	}

	void PathTracing::CreateComputePipeline()
	{
		// Reference mode (mode 0)
		CreateComputePipelineForMode(0, m_ComputeShader, m_ComputePipeline);
		// BUILD mode (mode 1)
		CreateComputePipelineForMode(1, m_BuildComputeShader, m_BuildComputePipeline);
		// FILL mode (mode 2)
		CreateComputePipelineForMode(2, m_FillComputeShader, m_FillComputePipeline);
	}

	void PathTracing::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

		auto* rts = renderer->GetRenderTargets();

		auto* rrInput = renderer->GetRRInput();

		auto* sp = renderer->GetStablePlanes();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(3, m_SHaRC->GetSHaRCConstantBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(6, m_SHaRC->GetResolveBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(7, m_SHaRC->GetHashEntriesBuffer()),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture()),
			nvrhi::BindingSetItem::Texture_UAV(1, rrInput->diffuseAlbedo),
			nvrhi::BindingSetItem::Texture_UAV(2, rrInput->specularAlbedo),
			nvrhi::BindingSetItem::Texture_UAV(3, rts->normalRoughness),
			nvrhi::BindingSetItem::Texture_UAV(4, rrInput->specularHitDistance),
			// Stable Planes UAVs
			nvrhi::BindingSetItem::Texture_UAV(5, sp->header),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(6, sp->buffer),
			nvrhi::BindingSetItem::Texture_UAV(7, sp->stableRadiance),
			// PT Motion Vectors output
			nvrhi::BindingSetItem::Texture_UAV(8, renderer->m_PTMotionVectors),
			// PT Depth output
			nvrhi::BindingSetItem::Texture_UAV(9, renderer->m_PTDepth),
			// ReSTIR GI: Secondary G-Buffer UAVs
			nvrhi::BindingSetItem::Texture_UAV(10, renderer->GetReSTIRGIResources()->secondaryGBufferPositionNormal),
			nvrhi::BindingSetItem::Texture_UAV(11, renderer->GetReSTIRGIResources()->secondaryGBufferRadiance),
			nvrhi::BindingSetItem::Texture_UAV(12, renderer->GetReSTIRGIResources()->secondaryGBufferDiffuseAlbedo),
			nvrhi::BindingSetItem::Texture_UAV(13, renderer->GetReSTIRGIResources()->secondaryGBufferSpecularF0Roughness),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(14, renderer->GetReSTIRGIResources()->surfaceDataBuffer)
		};

		
#if defined(NVAPI)
		bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_UAV(127, nullptr));
#endif

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void PathTracing::ExecuteDispatch(nvrhi::ICommandList* commandList,
		nvrhi::rt::PipelineHandle rayPipeline, nvrhi::rt::ShaderTableHandle shaderTable,
		nvrhi::ComputePipelineHandle computePipeline)
	{
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable,
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable()
		};

		auto resolution = Renderer::GetSingleton()->GetDynamicResolution();

		if (rayPipeline)
		{
			nvrhi::rt::State state;
			state.shaderTable = shaderTable;
			state.bindings = bindings;
			commandList->setRayTracingState(state);

			nvrhi::rt::DispatchRaysArguments args;
			args.width = resolution.x;
			args.height = resolution.y;
			commandList->dispatchRays(args);
		}
		else if (computePipeline)
		{
			nvrhi::ComputeState state;
			state.pipeline = computePipeline;
			state.bindings = bindings;
			commandList->setComputeState(state);

			auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 32);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}
	}

	void PathTracing::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		if (m_UseStablePlanes)
		{
			// Two-pass stable planes: BUILD then FILL
			ExecuteDispatch(commandList, m_BuildRayPipeline, m_BuildShaderTable, m_BuildComputePipeline);
			ExecuteDispatch(commandList, m_FillRayPipeline, m_FillShaderTable, m_FillComputePipeline);
		}
		else
		{
			// Reference mode: single pass
			ExecuteDispatch(commandList, m_RayPipeline, m_ShaderTable, m_ComputePipeline);
		}
	}
}