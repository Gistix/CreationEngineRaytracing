#include "State.h"
#include "Utils.h"

#include "framework/DescriptorTableManager.h"

void State::InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
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

bool State::Initialize(ID3D12Device5* d3d12Device, ID3D12CommandQueue* commandQueue)
{
	InitializeLog();

	// NVRHI Device
	nvrhi::d3d12::DeviceDesc deviceDesc;
	deviceDesc.errorCB = &MessageCallback::GetInstance();
	deviceDesc.pDevice = d3d12Device;
	deviceDesc.pGraphicsCommandQueue = commandQueue;

	m_NVRHIDevice = nvrhi::d3d12::createDevice(deviceDesc);

	device = d3d12Device;

	CreateRootSignature();

	m_ConstantBuffer = m_NVRHIDevice->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(FrameData), "LightingConstants", Constants::MAX_CB_VERSIONS));

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

	return true;
}

void State::CreateRootSignature()
{
	nvrhi::BindlessLayoutDesc bindlessLayoutDesc;
	bindlessLayoutDesc.visibility = nvrhi::ShaderType::All;
	bindlessLayoutDesc.firstSlot = 0;
	bindlessLayoutDesc.maxCapacity = 1024;
	bindlessLayoutDesc.registerSpaces = {
		nvrhi::BindingLayoutItem::RawBuffer_SRV(1),
		nvrhi::BindingLayoutItem::Texture_SRV(2)
	};

	m_BindlessLayout = m_NVRHIDevice->createBindlessLayout(bindlessLayoutDesc);

	nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
	globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
	globalBindingLayoutDesc.bindings = {
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
		nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
		nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
		nvrhi::BindingLayoutItem::Sampler(0),
		nvrhi::BindingLayoutItem::Texture_UAV(0)
	};
	m_BindingLayout = m_NVRHIDevice->createBindingLayout(globalBindingLayoutDesc);

	m_DescriptorTable = std::make_shared<DescriptorTableManager>(m_NVRHIDevice, m_BindlessLayout);
}

bool State::CreateRayTracingPipeline()
{
	nvrhi::rt::PipelineDesc pipelineDesc;
	pipelineDesc.globalBindingLayouts = { m_BindingLayout, m_BindlessLayout };
	//pipelineDesc.shaders

	eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"0" } };

	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/RayGeneration.hlsl", defines);
	auto rayGenHandle = m_NVRHIDevice->createShader({ nvrhi::ShaderType::RayGeneration, "", "Main"}, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

	winrt::com_ptr<IDxcBlob> missBlob;
	ShaderUtils::CompileShader(missBlob, L"data/shaders/raytracing/Miss.hlsl", defines);
	auto missHandle = m_NVRHIDevice->createShader({ nvrhi::ShaderType::Miss, "", "Main" }, missBlob->GetBufferPointer(), missBlob->GetBufferSize());

	winrt::com_ptr<IDxcBlob> closestHitBlob;
	ShaderUtils::CompileShader(closestHitBlob, L"data/shaders/raytracing/ClosestHit.hlsl", defines);
	auto closestHitHandle = m_NVRHIDevice->createShader({ nvrhi::ShaderType::ClosestHit, "", "Main" }, closestHitBlob->GetBufferPointer(), closestHitBlob->GetBufferSize());

	pipelineDesc.shaders = {
		{ "", rayGenHandle, nullptr },
		{ "", missHandle, nullptr }
	};

	pipelineDesc.hitGroups = { {
		"HitGroup",
		closestHitHandle, // m_ShaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit),
		nullptr, // m_ShaderLibrary->getShader("AnyHit", nvrhi::ShaderType::AnyHit),
		nullptr, // intersectionShader
		nullptr, // bindingLayout
		false  // isProceduralPrimitive
	} };

	pipelineDesc.maxPayloadSize = 20;

	pipelineDesc.allowOpacityMicromaps = true;

	m_RayPipeline = m_NVRHIDevice->createRayTracingPipeline(pipelineDesc);

	if (!m_RayPipeline)
		return false;

	auto& shaderTableDesc = nvrhi::rt::ShaderTableDesc()
		.enableCaching(3)
		.setDebugName("Shader Table");
	m_ShaderTable = m_RayPipeline->createShaderTable(shaderTableDesc);

	if (!m_ShaderTable)
		return false;

	m_ShaderTable->setRayGenerationShader("RayGen");
	m_ShaderTable->addHitGroup("HitGroup");
	m_ShaderTable->addMissShader("Miss");

	return true;
}

bool State::CreateComputePipeline()
{
	eastl::vector<DxcDefine> defines = { { L"USE_RAY_QUERY", L"1" } };

	winrt::com_ptr<IDxcBlob> rayGenBlob;
	ShaderUtils::CompileShader(rayGenBlob, L"shaders/raytracing/RayGeneration.hlsl", defines);
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

void State::UpdateFrameBuffer(float4x4 viewInverse, float4x4 projInverse, float4 cameraData, float4 NDCToView, float3 position) const
{
	frameData->ViewInverse = viewInverse;
	frameData->ProjInverse = projInverse;
	frameData->CameraData = cameraData;
	frameData->NDCToView = NDCToView;
	frameData->Position = position;
}

void State::AttachModel([[maybe_unused]] RE::TESForm* form) {
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

	if (Utils::IsPlayer(refr)) {
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

void State::AttachLand([[maybe_unused]] RE::TESForm* form, [[maybe_unused]] RE::NiAVObject* root) {

}