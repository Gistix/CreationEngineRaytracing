#include "GBuffer.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"

namespace Pass::Raytracing::PathTracing
{
	GBuffer::GBuffer(Renderer* renderer, SceneTLAS* sceneTLAS)
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

	void GBuffer::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void GBuffer::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
	}

	void GBuffer::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetPathTracingDefines(settings, false, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreateBindingLayout();
			CreatePipeline();
			m_BindingSetDirty.fill(true);
		}
	}

	void GBuffer::CreateBindingLayout()
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
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),
			nvrhi::BindingLayoutItem::Texture_SRV(8),
			nvrhi::BindingLayoutItem::Texture_SRV(9),           // Water displacement
			nvrhi::BindingLayoutItem::Texture_SRV(10),          // Projection noise
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11), // Transforms
			nvrhi::BindingLayoutItem::RawBuffer_SRV(19),        // MeshSlotRemap
			nvrhi::BindingLayoutItem::RawBuffer_SRV(20),        // PropertiesBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(0),           // Albedo
			nvrhi::BindingLayoutItem::Texture_UAV(1),           // EmissiveMetallic
			nvrhi::BindingLayoutItem::Texture_UAV(2),           // NormalRoughness
			nvrhi::BindingLayoutItem::Texture_UAV(3),           // MotionVectors
			nvrhi::BindingLayoutItem::Texture_UAV(4),           // Depth
			nvrhi::BindingLayoutItem::Texture_UAV(5)            // Material
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	bool GBuffer::CreateRayTracingPipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"0");
		eastl::vector<DxcDefine> commonDefines;

		auto device = GetRenderer()->GetDevice();

		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/GBuffer/RayGeneration.hlsl", defines);
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

	bool GBuffer::CreateComputePipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto device = GetRenderer()->GetDevice();

		auto rayGenBlob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/GBuffer/RayGeneration.hlsl", defines, ShaderStage::Compute);
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

	void GBuffer::CreatePipeline()
	{
		const auto renderer = GetRenderer();

		// Ray query is faster and SER is not recommended for primary rays, especially ours which are very simple
		// Its possible that the statement above do not hold true for AMD or RTX 50 series
		if (renderer->IsFeatureSupported(nvrhi::Feature::RayQuery)) {
			CreateComputePipeline();
		} else {
			CreateRayTracingPipeline();
		}
	}

	void GBuffer::CheckBindings()
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
			nvrhi::BindingSetItem::Texture_SRV(2, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(8, scene->GetSkinDetailNormalTexture()),
			nvrhi::BindingSetItem::Texture_SRV(9, renderer->GetWaterDisplacementTexture()),
			nvrhi::BindingSetItem::Texture_SRV(10, scene->GetProjNoiseTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(11, sceneGraph->GetTransformBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(19, sceneGraph->GetMeshSlotRemapBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(20, sceneGraph->GetPropertiesBuffer()),
			nvrhi::BindingSetItem::Texture_UAV(0, textureManager.GetTexture(RenderTarget::Albedo)),
			nvrhi::BindingSetItem::Texture_UAV(1, textureManager.GetTexture(RenderTarget::EmissiveMetallic)),
			nvrhi::BindingSetItem::Texture_UAV(2, textureManager.GetTexture(RenderTarget::NormalRoughness)),
			nvrhi::BindingSetItem::Texture_UAV(3, textureManager.GetTexture(RenderTarget::MotionVectors3D)),
			nvrhi::BindingSetItem::Texture_UAV(4, textureManager.GetTexture(RenderTarget::ClipDepth)),
			nvrhi::BindingSetItem::Texture_UAV(5, textureManager.GetTexture(RenderTarget::Material))
		};

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
		m_BindingSetDirty[currentSlot] = false;
	}

	void GBuffer::Execute(nvrhi::ICommandList* commandList)
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
