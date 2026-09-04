#include "PathTracing.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"

namespace Pass
{
	PathTracing::PathTracing(Renderer* renderer, SceneTLAS* sceneTLAS, SHaRC* sharc)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS), m_SHaRC(sharc)
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

		m_Defines = Util::Shader::GetPathTracingDefines(settings, m_SHaRC != nullptr, false);

		m_UseStablePlanes = settings.AdvancedSettings.StablePlanes;
		m_UseRestirGI = settings.ReSTIRGI.Enabled;

		m_SceneTLAS->GetTopLevelAS().AddListener(this);
	}

	void PathTracing::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void PathTracing::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
	}

	void PathTracing::SettingsChanged(const Settings& settings)
	{
		m_UseStablePlanes = settings.AdvancedSettings.StablePlanes;
		m_UseRestirGI = settings.ReSTIRGI.Enabled;

		auto defines = Util::Shader::GetPathTracingDefines(settings, m_SHaRC != nullptr, false);

		if (defines != m_Defines) {
			m_Defines = defines;
			CreateBindingLayout();
			CreatePipeline();
			m_BindingSetDirty.fill(true);
		}
	}

	void PathTracing::CreateBindingLayout()
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
			nvrhi::BindingLayoutItem::Texture_SRV(2),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),
			nvrhi::BindingLayoutItem::Texture_SRV(8),
			nvrhi::BindingLayoutItem::Texture_SRV(9),           // Water displacement
			nvrhi::BindingLayoutItem::Texture_SRV(10),          // Projection noise
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11), // Transforms
			nvrhi::BindingLayoutItem::RawBuffer_SRV(19),        // MeshSlotRemap
			nvrhi::BindingLayoutItem::RawBuffer_SRV(20),        // PropertiesBuffer
			nvrhi::BindingLayoutItem::Texture_UAV(0),           // Color output
			nvrhi::BindingLayoutItem::Texture_UAV(1),           // Normal Roughness
			nvrhi::BindingLayoutItem::Texture_UAV(2),           // MotionVectors (RWTexture2D<float4>)
			nvrhi::BindingLayoutItem::Texture_UAV(3)            // Depth (RWTexture2D<float>)
		};

		const auto& settings = Scene::GetSingleton()->m_Settings;

		if (settings.SHaRCSettings.Enabled) {
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(3));
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6));
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7));
		}

		const bool nrd = (settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
			settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);
		const bool dlssrr = (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR);

		if (nrd || dlssrr) {
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(4)); // Diffuse Albedo

			if (nrd) {
				globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(5)); // ViewZ

				globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(6)); // Diffuse Radiance
				globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(7)); // Specular Radiance

				globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(8)); // Diffuse Factor
				globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(9)); // Specular Factor
			}
			else {
				globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(5)); // Specular Albedo (EnvBRDF)
				globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(6)); // Specular Hit Distance
			}
		}

		if (m_UseStablePlanes) {
			// Stable Planes / PSR UAVs
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(10)); // PSR_RaySegment
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(11)); // PSR_Throughput
		}

		if (m_UseRestirGI) {
			// ReSTIR GI: Secondary G-Buffer UAVs
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(13)); // SecondaryGBufPositionNormal
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(14)); // SecondaryGBufRadiance
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(15)); // SecondaryGBufDiffuseAlbedo
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(16)); // SecondaryGBufSpecularRough

			// ReSTIR GI: Packed primary surface data (ping-pong StructuredBuffer)
			globalBindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(17)); // SurfaceDataBuffer
		}

#if defined(NVAPI)
		if (!GetRenderer()->m_Settings.UseRayQuery)
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

	void PathTracing::CreateRayTracingPipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"0");
		eastl::vector<DxcDefine> commonDefines;

		auto device = GetRenderer()->GetDevice();

		auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines);
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

	void PathTracing::CreateComputePipeline()
	{
		auto defines = Util::Shader::GetDXCDefines(m_Defines);
		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto device = GetRenderer()->GetDevice();

		auto rayGenBlob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines, ShaderStage::Compute);
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

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
	}

	void PathTracing::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();

		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

		auto* rts = renderer->GetRenderTargets();

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
			nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::Texture_SRV(8, scene->GetSkinDetailNormalTexture()),
			nvrhi::BindingSetItem::Texture_SRV(9, renderer->GetWaterDisplacementTexture()),
			nvrhi::BindingSetItem::Texture_SRV(10, scene->GetProjNoiseTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(11, sceneGraph->GetTransformBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(19, sceneGraph->GetMeshSlotRemapBuffer()),
			nvrhi::BindingSetItem::RawBuffer_SRV(20, sceneGraph->GetPropertiesBuffer()),
			nvrhi::BindingSetItem::Texture_UAV(0, renderer->GetMainTexture()),
			nvrhi::BindingSetItem::Texture_UAV(1, rts->normalRoughness),
			nvrhi::BindingSetItem::Texture_UAV(2, textureManager.GetTexture(RenderTarget::MotionVectors3D)),
			nvrhi::BindingSetItem::Texture_UAV(3, textureManager.GetTexture(RenderTarget::ClipDepth))
		};

		const auto& settings = scene->m_Settings;

		if (settings.SHaRCSettings.Enabled) {
			bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(3, m_SHaRC->GetSHaRCConstantBuffer()));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(6, m_SHaRC->GetResolveBuffer()));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(7, m_SHaRC->GetHashEntriesBuffer()));
		}

		const bool nrd = (settings.GeneralSettings.Denoiser == Denoiser::NRD_Reblur ||
			settings.GeneralSettings.Denoiser == Denoiser::NRD_Relax);
		const bool dlssrr = (settings.GeneralSettings.Denoiser == Denoiser::DLSS_RR);

		if (nrd || dlssrr) {
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(4, textureManager.GetTexture(RenderTarget::DiffuseAlbedo)));

			if (nrd) {
				bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(5, textureManager.GetTexture(RenderTarget::ViewDepth)));
				bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(6, textureManager.GetTexture(RenderTarget::DiffuseRadiance)));
				bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(7, textureManager.GetTexture(RenderTarget::SpecularRadiance)));
				bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(8, textureManager.GetTexture(RenderTarget::DiffuseFactor)));
				bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(9, textureManager.GetTexture(RenderTarget::SpecularFactor)));
			}
			else {
				bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(5, textureManager.GetTexture(RenderTarget::RRSpecularAlbedo)));
				bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(6, textureManager.GetTexture(RenderTarget::RRSpecularHitDist)));
			}
		}

		if (m_UseStablePlanes) {
			// Stable Planes / PSR UAVs
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(10, textureManager.GetTexture(RenderTarget::PSR_RaySegment)));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(11, textureManager.GetTexture(RenderTarget::PSR_Throughput)));
		}

		if (m_UseRestirGI) {
			auto* rgi = renderer->GetReSTIRGIResources();

			// ReSTIR GI: Secondary G-Buffer UAVs
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(13, rgi->secondaryGBufferPositionNormal));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(14, rgi->secondaryGBufferRadiance));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(15, rgi->secondaryGBufferDiffuseAlbedo));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(16, rgi->secondaryGBufferSpecularF0Roughness));
			bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(17, rgi->surfaceDataBuffer));
		}

#if defined(NVAPI)
		if (!renderer->m_Settings.UseRayQuery)
			bindingSetDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_UAV(127, nullptr));
#endif

		m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		if (!m_BindingSets[currentSlot]) {

			for (const auto& binding : bindingSetDesc.bindings)
			{
				logger::info("PathTracing::CheckBindings - {}, {}, 0x{:08X}", magic_enum::enum_name(binding.type), binding.slot, reinterpret_cast<uintptr_t>(binding.resourceHandle));
			}
		}

		m_BindingSetDirty[currentSlot] = false;
	}

	void PathTracing::ExecuteDispatch(nvrhi::ICommandList* commandList,
		nvrhi::rt::PipelineHandle rayPipeline, nvrhi::rt::ShaderTableHandle shaderTable,
		nvrhi::ComputePipelineHandle computePipeline)
	{
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

			auto threadGroupSize = Util::Math::GetDispatchCount(resolution, Constants::PT_DISPATCH_THREADS);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}
	}

	void PathTracing::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		ExecuteDispatch(commandList, m_RayPipeline, m_ShaderTable, m_ComputePipeline);
	}
}
