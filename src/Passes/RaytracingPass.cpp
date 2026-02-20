#include "RaytracingPass.h"
#include "Renderer.h"
#include "SceneGraph.h"

void RaytracingPass::Init()
{
	m_FrameData = eastl::make_unique<FrameData>();

	m_ConstantBuffer = Renderer::GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(FrameData), "Frame Data", Constants::MAX_CB_VERSIONS));

	m_LinearWrapSampler = Renderer::GetDevice()->createSampler(
		nvrhi::SamplerDesc()
		.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
		.setAllFilters(true));

	CreatePipeline();
}

void RaytracingPass::CreatePipeline()
{
	CreateRootSignature();

	if (Renderer::GetSingleton()->settings.UseRayQuery)
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
	m_BindingLayout = Renderer::GetDevice()->createBindingLayout(globalBindingLayoutDesc);

	nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
	bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
	bindlessLayoutDesc.firstSlot = 0;
	bindlessLayoutDesc.maxCapacity = 4096;
	bindlessLayoutDesc.registerSpaces = {
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2)
		//nvrhi::BindingLayoutItem::Texture_SRV(3)
	};
	m_BindlessLayout = Renderer::GetDevice()->createBindlessLayout(bindlessLayoutDesc);

	m_DescriptorTable = eastl::make_shared<DescriptorTableManager>(Renderer::GetDevice(), m_BindlessLayout);
}

bool RaytracingPass::CreateRayTracingPipeline()
{
	nvrhi::rt::PipelineDesc pipelineDesc;
	pipelineDesc.globalBindingLayouts = { m_BindingLayout, m_BindlessLayout };
	eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"0" } };

	// Compile Libraries
	auto rayGenLib = ShaderUtils::CompileShaderLibrary(Renderer::GetDevice(), L"data/shaders/raytracing/RayGeneration.hlsl", defines);
	auto missLib = ShaderUtils::CompileShaderLibrary(Renderer::GetDevice(), L"data/shaders/raytracing/Miss.hlsl", defines);
	auto hitLib = ShaderUtils::CompileShaderLibrary(Renderer::GetDevice(), L"data/shaders/raytracing/ClosestHit.hlsl", defines);

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

	m_RayPipeline = Renderer::GetDevice()->createRayTracingPipeline(pipelineDesc);
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

	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/RayGeneration.hlsl", defines);
	m_ComputeShader = Renderer::GetDevice()->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

	if (!m_ComputeShader)
		return false;

	auto pipelineDesc = nvrhi::ComputePipelineDesc()
		.setComputeShader(m_ComputeShader)
		.addBindingLayout(m_BindingLayout)
		.addBindingLayout(m_BindlessLayout);

	m_ComputePipeline = Renderer::GetDevice()->createComputePipeline(pipelineDesc);

	if (!m_ComputePipeline)
		return false;

	return true;
}

void RaytracingPass::UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const
{
	m_FrameData->ViewInverse = viewInverse;
	m_FrameData->ProjInverse = projInverse;
	m_FrameData->CameraData = cameraData;
	m_FrameData->NDCToView = NDCToView;
	m_FrameData->Position = position;
}

void RaytracingPass::UpdateAccelStructs(nvrhi::ICommandList* commandList)
{
	auto* sceneGraph = SceneGraph::GetSingleton();

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
		m_TopLevelAS = Renderer::GetDevice()->createAccelStruct(tlasDesc);

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

	nvrhi::BindingSetDesc bindingSetDesc;
	bindingSetDesc.bindings = {
		nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
		nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_TopLevelAS),
		nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
		nvrhi::BindingSetItem::Texture_UAV(0, Renderer::GetSingleton()->m_MainTexture)
	};

	m_BindingSet = Renderer::GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

	m_DirtyBindings = false;
}


void RaytracingPass::Execute(nvrhi::ICommandList* commandList)
{
	commandList->writeBuffer(m_ConstantBuffer, m_FrameData.get(), sizeof(FrameData));

	UpdateAccelStructs(commandList);

	CheckBindings();
}