#include "Debug.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"

namespace Pass
{
	Debug::Debug(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		auto device = renderer->GetDevice();

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

		CreateBindingLayout();
		CreatePipeline();
	}

	void Debug::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_DirtyBindings = true;
	}

	void Debug::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetPathTracingDefines(settings, false, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreateBindingLayout();
			CreatePipeline();
			m_DirtyBindings = true;
		}
	}

	void Debug::CreateBindingLayout()
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
			nvrhi::BindingLayoutItem::Texture_SRV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
			nvrhi::BindingLayoutItem::Texture_UAV(0)
		};

#if defined(NVAPI)
		globalBindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::TypedBuffer_UAV(127));
#endif

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void Debug::CreatePipeline()
	{
		if (GetRenderer()->m_Settings.UseRayQuery) {
			CreateComputePipeline();
		} else {
			CreateRayTracingPipeline();
		}
	}

	void Debug::CreateRayTracingPipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"0");

		auto device = GetRenderer()->GetDevice();

		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Debug/RayGeneration.hlsl", defines);
		auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/Miss.hlsl", defines);
		auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ClosestHit.hlsl", defines);
		auto anyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/AnyHit.hlsl", defines);
		auto shadowMissLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ShadowMiss.hlsl", defines);
		auto shadowAnyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ShadowAnyHit.hlsl", defines);

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
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout);

		pipelineDesc.maxPayloadSize = 20;

		// When enabled causes: D3D12 ERROR: ID3D12Device::CreateStateObject: Invalid D3D12_RAYTRACING_PIPELINE_CONFIG1.Flags: 0x1024 specified
		pipelineDesc.allowOpacityMicromaps = false;

#if defined(NVAPI)
		pipelineDesc.hlslExtensionsUAV = 127;
#endif

		m_RayPipeline = device->createRayTracingPipeline(pipelineDesc);
		if (!m_RayPipeline)
			return;

		auto shaderTableDesc = nvrhi::rt::ShaderTableDesc()
			.enableCaching(5)
			.setDebugName("Shader Table");

		m_ShaderTable = m_RayPipeline->createShaderTable(shaderTableDesc);
		if (!m_ShaderTable)
			return;

		m_ShaderTable->setRayGenerationShader("RayGen");
		m_ShaderTable->addMissShader("Miss");
		m_ShaderTable->addMissShader("ShadowMiss");
		m_ShaderTable->addHitGroup("HitGroup");
		m_ShaderTable->addHitGroup("ShadowHitGroup");
	}

	void Debug::CreateComputePipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto device = GetRenderer()->GetDevice();

		auto* rayGenBlob = ShaderCache::GetShader(L"data/shaders/raytracing/Debug/RayGeneration.hlsl", defines, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc().setComputeShader(m_ComputeShader);

		pipelineDesc.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void Debug::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

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
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture())
		};

#if defined(NVAPI)
		bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_UAV(127, nullptr));
#endif

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		if (!m_BindingSet) {

			for (const auto& binding : bindingSetDesc.bindings)
			{
				logger::info("Debug::CheckBindings - {}, {}, 0x{:08X}", magic_enum::enum_name(binding.type), binding.slot, reinterpret_cast<uintptr_t>(binding.resourceHandle));
			}
		}

		m_DirtyBindings = false;
	}

	void Debug::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable
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