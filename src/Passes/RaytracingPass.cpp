#include "RaytracingPass.h"
#include "Renderer.h"
#include "Scene.h"

RaytracingPass::RaytracingPass(Renderer* renderer) 
	: RenderPass(renderer)
{
	m_LinearWrapSampler = GetRenderer()->GetDevice()->createSampler(
		nvrhi::SamplerDesc()
		.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
		.setAllFilters(true));

	CreatePipeline();
}

void RaytracingPass::CreatePipeline()
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

void RaytracingPass::ResolutionChanged([[maybe_unused]] uint2 resolution)
{
	m_DirtyBindings = true;
}

void RaytracingPass::CreateRootSignature()
{
	nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
	globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
	globalBindingLayoutDesc.bindings = {
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
		nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
		nvrhi::BindingLayoutItem::Sampler(0),
		nvrhi::BindingLayoutItem::Texture_UAV(0)
	};
	m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);

	nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
	bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
	bindlessLayoutDesc.firstSlot = 0;
	bindlessLayoutDesc.maxCapacity = 4096;
	bindlessLayoutDesc.registerSpaces = {
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2)
		//nvrhi::BindingLayoutItem::Texture_SRV(3)
	};
	m_BindlessLayout = GetRenderer()->GetDevice()->createBindlessLayout(bindlessLayoutDesc);

	m_DescriptorTable = eastl::make_shared<DescriptorTableManager>(GetRenderer()->GetDevice(), m_BindlessLayout);
}

bool RaytracingPass::CreateRayTracingPipeline()
{
	nvrhi::rt::PipelineDesc pipelineDesc;
	pipelineDesc.globalBindingLayouts = { m_BindingLayout, m_BindlessLayout };
	eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"0" } };

	auto device = GetRenderer()->GetDevice();

	// Compile Libraries
	auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/RayGeneration.hlsl", defines);
	auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Miss.hlsl", defines);
	auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/ClosestHit.hlsl", defines);

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

bool RaytracingPass::CreateComputePipeline()
{
	eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"1" } };

	auto device = GetRenderer()->GetDevice();

	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/RayGeneration.hlsl", defines);
	m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

	if (!m_ComputeShader)
		return false;

	auto pipelineDesc = nvrhi::ComputePipelineDesc()
		.setComputeShader(m_ComputeShader)
		.addBindingLayout(m_BindingLayout)
		.addBindingLayout(m_BindlessLayout);

	m_ComputePipeline = GetRenderer()->GetDevice()->createComputePipeline(pipelineDesc);

	if (!m_ComputePipeline)
		return false;

	return true;
}

void RaytracingPass::UpdateAccelStructs(nvrhi::ICommandList* commandList)
{
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	instances.clear();
	instances.reserve(sceneGraph->instances.size());

	for (auto& instance : sceneGraph->instances)
	{
		instances.push_back(instance->GetInstanceDesc());
	}

	// Compact acceleration structures that are tagged for compaction and have finished executing the original build
	commandList->compactBottomLevelAccelStructs();

	uint32_t topLevelInstances = static_cast<uint32_t>(instances.size());

	if (!m_TopLevelAS || topLevelInstances > m_TopLevelInstances - Constants::NUM_INSTANCES_THRESHOLD) {
		float topLevelInstancesRatio = std::ceil(topLevelInstances / static_cast<float>(Constants::NUM_INSTANCES_STEP));

		uint32_t topLevelMaxInstances = static_cast<uint32_t>(topLevelInstancesRatio) * Constants::NUM_INSTANCES_STEP;

		m_TopLevelInstances = std::max(topLevelMaxInstances + Constants::NUM_INSTANCES_STEP, Constants::NUM_INSTANCES_MIN);

		nvrhi::rt::AccelStructDesc tlasDesc;
		tlasDesc.isTopLevel = true;
		tlasDesc.topLevelMaxInstances = m_TopLevelInstances;
		m_TopLevelAS = GetRenderer()->GetDevice()->createAccelStruct(tlasDesc);

		m_DirtyBindings = true;
	}

	commandList->beginMarker("TLAS Update");
	commandList->buildTopLevelAccelStruct(m_TopLevelAS, instances.data(), instances.size());
	commandList->endMarker();
}

void RaytracingPass::CheckBindings()
{
	if (!m_DirtyBindings)
		return;

	auto* renderer = GetRenderer();

	nvrhi::BindingSetDesc bindingSetDesc;
	bindingSetDesc.bindings = {
		nvrhi::BindingSetItem::ConstantBuffer(0, renderer->GetCameraDataBuffer()),
		nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_TopLevelAS),
		nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
		nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
	};

	m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

	m_DirtyBindings = false;
}

void RaytracingPass::Execute(nvrhi::ICommandList* commandList)
{
	UpdateAccelStructs(commandList);

	CheckBindings();

	auto resolution = Renderer::GetSingleton()->GetResolution();

	if (m_RayPipeline)
	{
		nvrhi::rt::State state;
		state.shaderTable = m_ShaderTable;
		state.bindings = { m_BindingSet, m_DescriptorTable->GetDescriptorTable() };
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
		state.bindings = { m_BindingSet, m_DescriptorTable->GetDescriptorTable() };
		commandList->setComputeState(state);

		auto threadGroupSize = Util::GetDispatchCount(resolution);

		commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
	}
}