#include "GlobalIllumination.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass::Raytracing
{
	GlobalIllumination::GlobalIllumination(Renderer* renderer, SceneTLAS* sceneTLAS, SHaRC* sharc)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_SHaRC(sharc)
	{
		m_LinearWrapSampler = GetRenderer()->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_Defines = Util::Shader::GetRaytracingDefines(Scene::GetSingleton()->m_Settings, true, false);

		m_SceneTLAS->GetTopLevelAS().AddListener(this);

		CreatePipeline();
	}

	void GlobalIllumination::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_DirtyBindings = true;
	}

	void GlobalIllumination::SettingsChanged(const Settings& settings)
	{
		auto defines = Util::Shader::GetRaytracingDefines(settings, true, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreatePipeline();
			m_DirtyBindings = true;
		}
	}

	void GlobalIllumination::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
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
			nvrhi::BindingLayoutItem::Texture_SRV(6),
			nvrhi::BindingLayoutItem::Texture_SRV(7),
			nvrhi::BindingLayoutItem::Texture_SRV(8),
			nvrhi::BindingLayoutItem::Texture_SRV(9),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11),
			nvrhi::BindingLayoutItem::Texture_UAV(0),
			nvrhi::BindingLayoutItem::Texture_UAV(1)
		};

		auto* scene = Scene::GetSingleton();
		auto& settings = scene->m_Settings;

		if (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR)
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2));

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void GlobalIllumination::CreatePipeline()
	{
		CreateBindingLayout();

		if (GetRenderer()->m_Settings.UseRayQuery)
			CreateComputePipeline();
		else
			CreateRayTracingPipeline();
	}

	void GlobalIllumination::CreateRayTracingPipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"0");

		auto device = GetRenderer()->GetDevice();

		// Compile Libraries
		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/GlobalIllumination/RayGeneration.hlsl", defines);
		auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/Miss.hlsl", defines);
		auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ClosestHit.hlsl", defines);
		auto anyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/AnyHit.hlsl", defines);

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
				anyHitLib->getShader("Main", nvrhi::ShaderType::AnyHit),
				nullptr,  // intersection
				nullptr,  // binding layout
				false     // isProceduralPrimitive
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

		m_RayPipeline = device->createRayTracingPipeline(pipelineDesc);
		if (!m_RayPipeline)
			return;

		auto shaderTableDesc = nvrhi::rt::ShaderTableDesc()
			.enableCaching(3)
			.setDebugName("Shader Table");

		m_ShaderTable = m_RayPipeline->createShaderTable(shaderTableDesc);
		if (!m_ShaderTable)
			return;

		m_ShaderTable->setRayGenerationShader("RayGen");
		m_ShaderTable->addMissShader("Miss");
		m_ShaderTable->addHitGroup("HitGroup");
	}

	void GlobalIllumination::CreateComputePipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/GlobalIllumination/RayGeneration.hlsl", defines, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void GlobalIllumination::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto& settings = scene->m_Settings;

		auto* sceneGraph = scene->GetSceneGraph();

		auto* renderTargets = renderer->GetRenderTargets();

		nvrhi::ITexture* diffuseTexture = nullptr;
		nvrhi::ITexture* specularTexture = nullptr;
		nvrhi::ITexture* specularHitDistTexture = nullptr;

		auto& textureManager = renderer->GetTextureManager();

		switch (settings.GeneralSettings.Denoiser)
		{
		case Denoiser::NRD_REBLUR:
		{
			diffuseTexture = textureManager.GetTexture(TextureManager::Texture::DiffuseRadiance);
			specularTexture = textureManager.GetTexture(TextureManager::Texture::SpecularRadiance);
			break;
		}
		case Denoiser::DLSS_RR:
		{
			auto* rrInput = renderer->GetRRInput();

			diffuseTexture = renderer->GetMainTexture();		
			specularTexture = rrInput->specularAlbedo;
			specularHitDistTexture = rrInput->specularHitDistance;
			break;
		}
		default:
			break;
		}

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
			nvrhi::BindingSetItem::ConstantBuffer(0, Scene::GetSingleton()->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SceneTLAS->GetRaytracingBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(3, m_SHaRC->GetSHaRCConstantBuffer()),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(6, renderer->GetDepthTexture()),
			nvrhi::BindingSetItem::Texture_SRV(7, renderTargets->albedo),
			nvrhi::BindingSetItem::Texture_SRV(8, renderTargets->normalRoughness),
			nvrhi::BindingSetItem::Texture_SRV(9, renderTargets->gnmao),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(10, m_SHaRC->GetResolveBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(11, m_SHaRC->GetHashEntriesBuffer()),
			nvrhi::BindingSetItem::Texture_UAV(0, diffuseTexture),
			nvrhi::BindingSetItem::Texture_UAV(1, specularTexture)
		};

		if (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR)
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(2, specularHitDistTexture));

		m_BindingSet = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void GlobalIllumination::Execute(nvrhi::ICommandList* commandList)
	{	
		CheckBindings();

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexDescriptors()->m_DescriptorTable,
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

			auto threadGroupSize = Util::Math::GetDispatchCount(resolution, 16);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}
	}
}
