#include "GBuffer.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Raytracing
{
	GBuffer::GBuffer(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		m_LinearWrapSampler = GetRenderer()->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_SceneTLAS->GetTopLevelAS().AddListener(this);

		CreatePipeline();
	}

	void GBuffer::CreatePipeline()
	{
		CreateRootSignature();

		auto* scene = Scene::GetSingleton();
		auto& rtSettings = scene->m_Settings.RaytracingSettings;

		const auto bouncesWStr = std::to_wstring(rtSettings.Bounces);
		const auto samplesWStr = std::to_wstring(rtSettings.SamplesPerPixel);

		eastl::vector<DxcDefine> defines = {
			{ L"DEBUG_TRACE_HEATMAP", L"0" }			
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

	void GBuffer::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_DirtyBindings = true;
	}

	void GBuffer::CreateRootSignature()
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

	bool GBuffer::CreateRayTracingPipeline(eastl::vector<DxcDefine>& defines)
	{
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		defines.emplace_back(L"USE_RAY_QUERY", L"0");

		auto device = GetRenderer()->GetDevice();

		// Compile Libraries
		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/GBuffer/RayGeneration.hlsl", defines);
		auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/Miss.hlsl", defines);
		auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ClosestHit.hlsl", defines);
		auto anyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/AnyHit.hlsl", defines);

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
			sceneGraph->GetTextureDescriptors()->m_Layout
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

	bool GBuffer::CreateComputePipeline(eastl::vector<DxcDefine>& defines)
	{
		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/GBuffer/RayGeneration.hlsl", defines, L"cs_6_5");
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

	void GBuffer::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

		auto* gBufferOutput = renderer->GetGBufferOutput();

		if (!m_ViewDepth)
		{
			auto resolution = renderer->GetResolution();

			nvrhi::TextureDesc desc;
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
			desc.keepInitialState = true;
			desc.isTypeless = false;
			desc.isUAV = true;
			desc.mipLevels = 1;
			desc.format = nvrhi::Format::R32_FLOAT;
			desc.debugName = "RTGBuffer Viewspace Depth";

			m_ViewDepth = renderer->GetDevice()->createTexture(desc);
		}

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::Texture_UAV(0, m_ViewDepth),
			nvrhi::BindingSetItem::Texture_UAV(1, gBufferOutput->motionVectors),
			nvrhi::BindingSetItem::Texture_UAV(2, gBufferOutput->albedo),
			nvrhi::BindingSetItem::Texture_UAV(3, gBufferOutput->normalRoughness),
			nvrhi::BindingSetItem::Texture_UAV(4, gBufferOutput->emissiveMetallic)
		};

		
#if defined(NVAPI)
		bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_UAV(127, nullptr));
#endif

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void GBuffer::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable,
			sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable()
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

		//auto region = nvrhi::TextureSlice{ 0, 0, 0, resolution.x, resolution.y, 1 };
		//commandList->copyTexture(GetRenderer()->GetMainTexture(), region, GetRenderer()->GetGBufferOutput()->motionVectors, region);
	}
}