#include "PathTracing.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	PathTracing::PathTracing(Renderer* renderer, RaytracingCommon* raytracingCommon)
		: RenderPass(renderer), m_RaytracingCommon(raytracingCommon)
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

		if (GetRenderer()->settings.UseRayQuery)
		{
			if (!CreateComputePipeline())
				return;
		}
		else
		{
			if (!CreateRayTracingPipeline())
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
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};
		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	bool PathTracing::CreateRayTracingPipeline()
	{
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"0" } };

		auto device = GetRenderer()->GetDevice();

		// Compile Libraries
		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines);
		auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/Miss.hlsl", defines);
		auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/ClosestHit.hlsl", defines);

		nvrhi::rt::PipelineDesc pipelineDesc;

		// Pipeline Shaders
		pipelineDesc.shaders = {
			{ "RayGen", rayGenLib->getShader("Main", nvrhi::ShaderType::RayGeneration), nullptr },
			{ "Miss", missLib->getShader("Main", nvrhi::ShaderType::Miss), nullptr },
		};

		pipelineDesc.hitGroups = {
			{
				"HitGroup",
				hitLib->getShader("Main", nvrhi::ShaderType::ClosestHit),
				nullptr,  // any hit
				nullptr,  // intersection
				nullptr,  // binding layout
				false     // isProceduralPrimitive
			}
		};

		pipelineDesc.globalBindingLayouts = {
			m_BindingLayout,
			sceneGraph->GetTriangleDescriptors()->m_Layout,
			sceneGraph->GetVertexDescriptors()->m_Layout,
			sceneGraph->GetTextureDescriptors()->m_Layout
		};

		pipelineDesc.maxPayloadSize = 20;
		pipelineDesc.allowOpacityMicromaps = true;

		m_RayPipeline = device->createRayTracingPipeline(pipelineDesc);
		if (!m_RayPipeline)
			return false;

		m_ShaderTable = m_RayPipeline->createShaderTable();
		if (!m_ShaderTable)
			return false;

		m_ShaderTable->setRayGenerationShader("RayGen");  // matches exportName above
		m_ShaderTable->addMissShader("Miss");            // see note below
		m_ShaderTable->addHitGroup("HitGroup");

		return true;
	}

	bool PathTracing::CreateComputePipeline()
	{
		eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"1" } };

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
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout);

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

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_RaytracingCommon->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_RaytracingCommon->GetTopLevelAS()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

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
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable()
		};

		auto resolution = Renderer::GetSingleton()->GetResolution();

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

			auto threadGroupSize = Util::GetDispatchCount(resolution, 16);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}
	}
}