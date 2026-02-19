#include "Scene.h"
#include "Util.h"

#include "SceneGraph.h"

#include "framework/DescriptorTableManager.h"

#include <nvrhi/validation.h>

void Scene::InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();

	*path /= std::format("{}.log"sv, "CreationEngineRaytracing");
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = a_level;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}

bool Scene::Initialize(ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue)
{
	InitializeLog();

	// NVRHI Device
	nvrhi::d3d12::DeviceDesc deviceDesc;
	deviceDesc.errorCB = &MessageCallback::GetInstance();
	deviceDesc.pDevice = d3d12Device;
	deviceDesc.pGraphicsCommandQueue = commandQueue;
	deviceDesc.aftermathEnabled = true;

	m_NVRHIDevice = nvrhi::d3d12::createDevice(deviceDesc);

	if (settings.ValidationLayer) 
	{
		nvrhi::DeviceHandle nvrhiValidationLayer = nvrhi::validation::createValidationLayer(m_NVRHIDevice);
		m_NVRHIDevice = nvrhiValidationLayer; // make the rest of the application go through the validation layer
	}

	device = d3d12Device;

	CreateRootSignature();

	if (settings.UseRayQuery)
	{
		if (!CreateComputePipeline())
			return false;
	}
	else
	{
		if (!CreateRayTracingPipeline())
			return false;
	}

	m_CommandList = m_NVRHIDevice->createCommandList();

	m_CommandList->open();

	return true;
}

void Scene::CreateRootSignature()
{
	nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
	bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
	bindlessLayoutDesc.firstSlot = 0;
	bindlessLayoutDesc.maxCapacity = 4096;
	bindlessLayoutDesc.registerSpaces = {
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2)
		//nvrhi::BindingLayoutItem::Texture_SRV(3)
	};

	m_BindlessLayout = m_NVRHIDevice->createBindlessLayout(bindlessLayoutDesc);

	nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
	globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
	globalBindingLayoutDesc.bindings = {
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
		nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
		nvrhi::BindingLayoutItem::Sampler(0),
		nvrhi::BindingLayoutItem::Texture_UAV(0)
	};
	m_BindingLayout = m_NVRHIDevice->createBindingLayout(globalBindingLayoutDesc);

	m_DescriptorTable = std::make_shared<DescriptorTableManager>(m_NVRHIDevice, m_BindlessLayout);
}

bool Scene::CreateRayTracingPipeline()
{
	nvrhi::rt::PipelineDesc pipelineDesc;
	pipelineDesc.globalBindingLayouts = { m_BindingLayout, m_BindlessLayout };
	eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"0" } };

	// Compile Libraries
	auto rayGenLib = ShaderUtils::CompileShaderLibrary(m_NVRHIDevice, L"data/shaders/raytracing/RayGeneration.hlsl", defines);
	auto missLib = ShaderUtils::CompileShaderLibrary(m_NVRHIDevice, L"data/shaders/raytracing/Miss.hlsl", defines);
	auto hitLib = ShaderUtils::CompileShaderLibrary(m_NVRHIDevice, L"data/shaders/raytracing/ClosestHit.hlsl", defines);

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

	m_RayPipeline = m_NVRHIDevice->createRayTracingPipeline(pipelineDesc);
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

bool Scene::CreateComputePipeline()
{
	eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"1" } };

	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/RayGeneration.hlsl", defines);
	m_ComputeShader = m_NVRHIDevice->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

	if (!m_ComputeShader)
		return false;

	auto pipelineDesc = nvrhi::ComputePipelineDesc()
		.setComputeShader(m_ComputeShader)
		.addBindingLayout(m_BindingLayout)
		.addBindingLayout(m_BindlessLayout);

	m_ComputePipeline = m_NVRHIDevice->createComputePipeline(pipelineDesc);

	if (!m_ComputePipeline)
		return false;

	return true;
}

void Scene::SetScreenSize(uint32_t width, uint32_t height)
{
	pendingRenderSize = { width, height };
}

void Scene::SetupResources()
{
	frameData = eastl::make_unique<FrameData>();

	m_ConstantBuffer = m_NVRHIDevice->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(FrameData), "Frame Buffer", Constants::MAX_CB_VERSIONS));

	m_LinearWrapSampler = m_NVRHIDevice->createSampler(
		nvrhi::SamplerDesc()
		.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
		.setAllFilters(true));
}

void Scene::CheckResolutionResources()
{
	if (renderSize == pendingRenderSize)
		return;

	renderSize = pendingRenderSize;

	// Output Texture
	{
		nvrhi::TextureDesc desc;
		desc.width = renderSize.x;
		desc.height = renderSize.y;
		desc.isUAV = true;
		desc.keepInitialState = true;
		desc.format = nvrhi::Format::RGBA16_FLOAT;
		desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		desc.debugName = "Output Texture";

		m_OutputTexture = m_NVRHIDevice->createTexture(desc);
	}

	dirtyBindings = true;
}

void Scene::UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const
{
	frameData->ViewInverse = viewInverse;
	frameData->ProjInverse = projInverse;
	frameData->CameraData = cameraData;
	frameData->NDCToView = NDCToView;
	frameData->Position = position;
}

void Scene::UpdateAccelStructs(nvrhi::ICommandList* commandList)
{
	auto* sceneGraph = SceneGraph::GetSingleton();

	instances.clear();
	instances.reserve(sceneGraph->instances.size());

	for (auto& instance: sceneGraph->instances)
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
		m_TopLevelAS = m_NVRHIDevice->createAccelStruct(tlasDesc);

		dirtyBindings = true;
	}

	commandList->beginMarker("TLAS Update");
	commandList->buildTopLevelAccelStruct(m_TopLevelAS, instances.data(), instances.size());
	commandList->endMarker();
}

void Scene::CheckBindings()
{
	if (!dirtyBindings)
		return;

	nvrhi::BindingSetDesc bindingSetDesc;
	bindingSetDesc.bindings = {
		nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
		nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_TopLevelAS),
		nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
		nvrhi::BindingSetItem::Texture_UAV(0, m_OutputTexture)
	};

	m_BindingSet = m_NVRHIDevice->createBindingSet(bindingSetDesc, m_BindingLayout);

	dirtyBindings = false;
}

void Scene::Execute()
{
	m_CommandList->writeBuffer(m_ConstantBuffer.Get(), frameData.get(), sizeof(FrameData));

	UpdateAccelStructs(m_CommandList);

	CheckResolutionResources();

	CheckBindings();

	m_CommandList->close();
	lastSubmittedInstance = m_NVRHIDevice->executeCommandList(m_CommandList.Get());

	m_NVRHIDevice->runGarbageCollection();
}

void Scene::WaitExecution() const
{
	m_NVRHIDevice->queueWaitForCommandList(nvrhi::CommandQueue::Graphics, nvrhi::CommandQueue::Graphics, lastSubmittedInstance);
	m_NVRHIDevice->runGarbageCollection();
}

void Scene::AttachModel([[maybe_unused]] RE::TESForm* form) {
	auto* refr = form->AsReference();

	auto* baseObject = refr->GetBaseObject();

	//auto flags = baseObject->GetFormFlags();
	RE::FormType type = baseObject->GetFormType();

	if (type == RE::FormType::IdleMarker)
		return;

	if (baseObject->IsMarker())
		return;

	//auto* node = refr->Get3D();

	if (auto* model = baseObject->As<RE::TESModel>()) {
		logger::info("[Raytracing] AttachModel - Model: {}", model->model);
		return;
	}

	if (Util::IsPlayer(refr)) {
		if (auto* player = reinterpret_cast<RE::PlayerCharacter*>(refr)) {
			// First Person
			//rt.CreateModelInternal(refr, std::format("{}_1stPerson", name).c_str(), pNiAVObject);

			// Third Person
			//rt.CreateActorModel(player, name, player->Get3D(false));
			logger::info("[Raytracing] AttachModel - Player: {}", player->GetName());
			return;
		}
	}

	if (auto* actor = refr->As<RE::Actor>()) {
		//rt.CreateActorModel(actor, actor->GetName(), pNiAVObject);
		logger::info("[Raytracing] AttachModel - Actor: {}", actor->GetName());
	}
}

void Scene::AttachLand([[maybe_unused]] RE::TESForm* form, [[maybe_unused]] RE::NiAVObject* root) {

}