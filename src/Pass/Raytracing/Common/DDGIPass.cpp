#include "DDGIPass.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"
#include "ShaderUtils.h"
#include "Utils/Shader.h"

namespace Pass::Raytracing::Common
{
	DDGIPass::DDGIPass(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		auto device = renderer->GetDevice();

		m_DefaultSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_ClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(false));

		m_PointWrapSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(false));

		m_BilinearClampSampler = device->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
			.setAllFilters(true));

		m_DDGIData = eastl::make_unique<DDGIData>();

		m_ConstantBuffer = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(DDGIData), "DDGI Constant Buffer", Constants::MAX_CB_VERSIONS));

		m_VolumeDescBuffer = Util::CreateStructuredBuffer<DDGIVolumeDescGPUPacked>(
			device, 1, "DDGI Volume Desc Structured Buffer", true);

		m_SceneTLAS->GetTopLevelAS().AddListener(this);

		SettingsChanged(Scene::GetSingleton()->m_Settings);
	}

	void DDGIPass::Initialize()
	{
		CreateTextures();
		SetupTracePass();
		SetupBlendPasses();
		SetupRelocationPass();
		SetupClassificationPass();
		SetupResolvePass();
	}

	void DDGIPass::ResolutionChanged([[maybe_unused]] uint2 resolution)
	{
		m_BindingSetDirty.fill(true);
	}

	void DDGIPass::SettingsChanged(const Settings& settings)
	{
		const bool wasEnabled = m_Enabled;
		m_Enabled = settings.DDGI.Enabled;
		m_DDGIData->DebugView = static_cast<uint32_t>(settings.DDGI.DebugView);

		const Int3 probeCounts = { settings.DDGI.ProbeCountX, settings.DDGI.ProbeCountY, settings.DDGI.ProbeCountZ };
		const float3 probeSpacing = { settings.DDGI.ProbeSpacing, settings.DDGI.ProbeSpacing, settings.DDGI.ProbeSpacing };
		const int raysPerProbe = settings.DDGI.RaysPerProbe;

		const bool gridChanged = (probeCounts.x != m_ProbeCounts.x || probeCounts.y != m_ProbeCounts.y || probeCounts.z != m_ProbeCounts.z ||
								  probeSpacing.x != m_ProbeSpacing.x || raysPerProbe != m_RaysPerProbe);

		m_ProbeCounts = probeCounts;
		m_ProbeSpacing = probeSpacing;
		m_RaysPerProbe = raysPerProbe;

		if (!m_Enabled)
		{
			m_TracePass.m_BindingSets.fill(nullptr);
			m_BlendIrradiancePass.m_BindingSets.fill(nullptr);
			m_BlendDistancePass.m_BindingSets.fill(nullptr);
			m_RelocationPass.m_BindingSets.fill(nullptr);
			m_ClassificationPass.m_BindingSets.fill(nullptr);
			m_ResolvePass.m_BindingSets.fill(nullptr);
			m_BindingSetDirty.fill(true);
			m_ResetCache = true;
			return;
		}

		if (!m_ProbeIrradiance || gridChanged)
		{
			CreateTextures();
			m_BindingSetDirty.fill(true);
			m_ResetCache = true;
		}

		if (m_Enabled && (!wasEnabled || gridChanged))
		{
			SetupTracePass();
			SetupBlendPasses();
			SetupRelocationPass();
			SetupClassificationPass();
			SetupResolvePass();
			m_BindingSetDirty.fill(true);
			m_ResetCache = true;
		}
	}

	void DDGIPass::CreateTextures()
	{
		auto device = GetRenderer()->GetDevice();

		// Probe Irradiance: 8x8 octahedral mapping per probe
		nvrhi::TextureDesc irrDesc;
		irrDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
		irrDesc.width = 8 * m_ProbeCounts.x;
		irrDesc.height = 8 * m_ProbeCounts.y;
		irrDesc.arraySize = m_ProbeCounts.z;
		irrDesc.format = nvrhi::Format::RGBA16_FLOAT;
		irrDesc.isUAV = true;
		irrDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		irrDesc.keepInitialState = true;
		irrDesc.debugName = "DDGI Probe Irradiance";
		m_ProbeIrradiance = device->createTexture(irrDesc);
		irrDesc.debugName = "DDGI Probe Variability";
		m_ProbeVariability = device->createTexture(irrDesc);

		// Probe Distance: 16x16 octahedral mapping per probe (R16G16: mean dist, mean dist^2)
		nvrhi::TextureDesc distDesc;
		distDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
		distDesc.width = 16 * m_ProbeCounts.x;
		distDesc.height = 16 * m_ProbeCounts.y;
		distDesc.arraySize = m_ProbeCounts.z;
		distDesc.format = nvrhi::Format::RG16_FLOAT;
		distDesc.isUAV = true;
		distDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		distDesc.keepInitialState = true;
		distDesc.debugName = "DDGI Probe Distance";
		m_ProbeDistance = device->createTexture(distDesc);

		// Probe Data: probe offsets (xyz) and classification status (w)
		nvrhi::TextureDesc dataDesc;
		dataDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
		dataDesc.width = m_ProbeCounts.x;
		dataDesc.height = m_ProbeCounts.y;
		dataDesc.arraySize = m_ProbeCounts.z;
		dataDesc.format = nvrhi::Format::RGBA16_FLOAT;
		dataDesc.isUAV = true;
		dataDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		dataDesc.keepInitialState = true;
		dataDesc.debugName = "DDGI Probe Data";
		m_ProbeData = device->createTexture(dataDesc);

		// Ray Data: width = rays per probe, height = probes per plane, arraySize = planes
		nvrhi::TextureDesc rayDesc;
		rayDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
		rayDesc.width = m_RaysPerProbe;
		rayDesc.height = m_ProbeCounts.x * m_ProbeCounts.y;
		rayDesc.arraySize = m_ProbeCounts.z;
		rayDesc.format = nvrhi::Format::RGBA16_FLOAT;
		rayDesc.isUAV = true;
		rayDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
		rayDesc.keepInitialState = true;
		rayDesc.debugName = "DDGI Ray Data";
		m_RayData = device->createTexture(rayDesc);

		logger::info("[DDGI] Created textures for grid {}x{}x{}, {} rays/probe",
			m_ProbeCounts.x, m_ProbeCounts.y, m_ProbeCounts.z, m_RaysPerProbe);
	}

	void DDGIPass::SetupTracePass()
	{
		auto device = GetRenderer()->GetDevice();
		auto const useRayQuery = GetRenderer()->m_Settings.UseRayQuery;

		m_TracePass.m_ComputeShader = nullptr;
		m_TracePass.m_ComputePipeline = nullptr;
		m_TracePass.m_RayPipeline = nullptr;
		m_TracePass.m_ShaderTable = nullptr;

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = useRayQuery ? nvrhi::ShaderType::Compute : nvrhi::ShaderType::AllRayTracing;
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // b0: Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // b1: DDGI
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(2), // b2: Features
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(3), // b3: Raytracing
			nvrhi::BindingLayoutItem::Texture_UAV(0),            // u0: RayData
			nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),  // t0: Scene TLAS
			nvrhi::BindingLayoutItem::Texture_SRV(1),            // t1: SkyHemisphere
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),   // t2: Lights
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),   // t3: Instances
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),   // t4: Meshes
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),   // t5: Transforms
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6),   // t6: InstanceLightList
			nvrhi::BindingLayoutItem::Texture_SRV(7),            // t7: ProbeData
			nvrhi::BindingLayoutItem::Texture_SRV(8),            // t8: ProbeIrradiance
			nvrhi::BindingLayoutItem::Texture_SRV(9),            // t9: ProbeDistance
			nvrhi::BindingLayoutItem::RawBuffer_SRV(10),         // t10: MeshSlotRemap
			nvrhi::BindingLayoutItem::RawBuffer_SRV(11),         // t11: PropertiesBuffer
			nvrhi::BindingLayoutItem::Texture_SRV(12),           // t12: WaterDisplacementMap
			nvrhi::BindingLayoutItem::Texture_SRV(13),           // t13: SkinDetailNormal
			nvrhi::BindingLayoutItem::Texture_SRV(14),           // t14: ProjNoiseMap
			nvrhi::BindingLayoutItem::Texture_SRV(15),           // t15: WaterFlowMap
			nvrhi::BindingLayoutItem::Sampler(0),                // s0: DefaultSampler
			nvrhi::BindingLayoutItem::Sampler(1),                // s1: ClampSampler
			nvrhi::BindingLayoutItem::Sampler(2),                // s2: PointWrapSampler
			nvrhi::BindingLayoutItem::Sampler(3)                 // s3: BilinearClampSampler
		};

		m_TracePass.m_BindingLayout = device->createBindingLayout(layoutDesc);

		auto defines = Util::Shader::GetDDGIDefines(Scene::GetSingleton()->m_Settings);
		auto dxcDefines = Util::Shader::GetDXCDefines(defines);

		if (useRayQuery)
		{
			dxcDefines.emplace_back(DxcDefine{ L"USE_RAY_QUERY", L"1" });

			winrt::com_ptr<IDxcBlob> blob;
			ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/DDGI/DDGIProbeTrace.hlsl", dxcDefines, ShaderStage::Compute);
			if (!blob)
			{
				logger::error("DDGIPass::SetupTracePass - Failed to compile compute shader.");
				return;
			}

			m_TracePass.m_ComputeShader = device->createShader(
				{ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (!m_TracePass.m_ComputeShader)
			{
				logger::error("DDGIPass::SetupTracePass - Failed to create Vulkan compute shader module.");
				return;
			}

			auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
			auto pipelineDesc = nvrhi::ComputePipelineDesc()
				.setComputeShader(m_TracePass.m_ComputeShader)
				.addBindingLayout(m_TracePass.m_BindingLayout)
				.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout);

			m_TracePass.m_ComputePipeline = device->createComputePipeline(pipelineDesc);
			if (!m_TracePass.m_ComputePipeline)
			{
				logger::error("DDGIPass::SetupTracePass - Failed to create compute pipeline.");
				return;
			}
		}
		else
		{
			dxcDefines.emplace_back(DxcDefine{ L"USE_RAY_QUERY", L"0" });

			eastl::vector<DxcDefine> commonDefines;
			auto rayGenLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/DDGI/DDGIProbeTrace.hlsl", dxcDefines);
			auto missLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/Miss.hlsl", commonDefines);
			auto hitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ClosestHit.hlsl", commonDefines);
			auto anyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/AnyHit.hlsl", dxcDefines);
			auto shadowMissLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ShadowMiss.hlsl", commonDefines);
			auto shadowAnyHitLib = ShaderUtils::CompileShaderLibrary(device, L"data/shaders/raytracing/Common/ShadowAnyHit.hlsl", dxcDefines);

			if (!rayGenLib || !missLib || !hitLib || !anyHitLib || !shadowMissLib || !shadowAnyHitLib)
			{
				logger::error("DDGIPass::SetupTracePass - Failed to compile one or more ray tracing shader libraries.");
				return;
			}

			nvrhi::rt::PipelineDesc pipelineDesc;
			pipelineDesc.shaders = {
				{ "RayGen", rayGenLib->getShader("Main", nvrhi::ShaderType::RayGeneration), nullptr },
				{ "Miss", missLib->getShader("Main", nvrhi::ShaderType::Miss), nullptr },
				{ "ShadowMiss", shadowMissLib->getShader("Main", nvrhi::ShaderType::Miss), nullptr }
			};

			pipelineDesc.hitGroups = {
				{ "HitGroup", hitLib->getShader("Main", nvrhi::ShaderType::ClosestHit), anyHitLib->getShader("Main", nvrhi::ShaderType::AnyHit), nullptr, nullptr, false },
				{ "ShadowHitGroup", nullptr, shadowAnyHitLib->getShader("Main", nvrhi::ShaderType::AnyHit), nullptr, nullptr, false }
			};

			auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
			pipelineDesc.globalBindingLayouts = {
				m_TracePass.m_BindingLayout,
				sceneGraph->GetTriangleDescriptors()->m_Layout,
				sceneGraph->GetVertexDescriptors()->m_Layout,
				sceneGraph->GetMaterialDescriptors()->m_Layout,
				sceneGraph->GetTextureDescriptors()->m_Layout,
				sceneGraph->GetCubemapDescriptors()->m_Layout,
				sceneGraph->GetDynamicVertexDescriptors()->m_Layout
			};

			pipelineDesc.maxPayloadSize = 20;
			pipelineDesc.allowOpacityMicromaps = false;

			m_TracePass.m_RayPipeline = device->createRayTracingPipeline(pipelineDesc);
			if (!m_TracePass.m_RayPipeline)
			{
				logger::error("DDGIPass::SetupTracePass - Failed to create ray tracing pipeline.");
				return;
			}
			if (m_TracePass.m_RayPipeline)
			{
				auto tableDesc = nvrhi::rt::ShaderTableDesc().enableCaching(5).setDebugName("DDGI Probe Trace Shader Table");
				m_TracePass.m_ShaderTable = m_TracePass.m_RayPipeline->createShaderTable(tableDesc);
				m_TracePass.m_ShaderTable->setRayGenerationShader("RayGen");
				m_TracePass.m_ShaderTable->addMissShader("Miss");
				m_TracePass.m_ShaderTable->addMissShader("ShadowMiss");
				m_TracePass.m_ShaderTable->addHitGroup("HitGroup");
				m_TracePass.m_ShaderTable->addHitGroup("ShadowHitGroup");
			}
		}

		m_TracePass.m_Initialized = true;
	}

	void DDGIPass::SetupBlendPasses()
	{
		auto device = GetRenderer()->GetDevice();
		m_BlendIrradiancePass.m_ComputeShader = nullptr;
		m_BlendIrradiancePass.m_ComputePipeline = nullptr;
		m_BlendIrradiancePass.m_Initialized = false;
		m_BlendDistancePass.m_ComputeShader = nullptr;
		m_BlendDistancePass.m_ComputePipeline = nullptr;
		m_BlendDistancePass.m_Initialized = false;

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::Compute;
		if (GetRenderer()->IsVulkan())
			layoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets()
				.setShaderResourceOffset(0)
				.setUnorderedAccessViewOffset(1));
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::PushConstants(1, sizeof(DDGIRootConstants)),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0), // t0: VolumeDesc
			nvrhi::BindingLayoutItem::Texture_UAV(0),           // u0: RayData
			nvrhi::BindingLayoutItem::Texture_UAV(1),           // u1: Output (Irradiance or Distance)
			nvrhi::BindingLayoutItem::Texture_UAV(2),           // u2: ProbeData
			nvrhi::BindingLayoutItem::Texture_UAV(3)            // u3: ProbeVariability
		};

		m_BlendIrradiancePass.m_BindingLayout = device->createBindingLayout(layoutDesc);
		m_BlendDistancePass.m_BindingLayout = m_BlendIrradiancePass.m_BindingLayout;

		eastl::vector<DxcDefine> commonDefines;

		// Blend Irradiance
		winrt::com_ptr<IDxcBlob> irrBlob;
		ShaderUtils::CompileShader(irrBlob, L"data/shaders/raytracing/DDGI/DDGIProbeBlendIrradiance.hlsl", commonDefines, ShaderStage::Compute, L"DDGIProbeBlendingCS");
		if (irrBlob)
		{
			m_BlendIrradiancePass.m_ComputeShader = device->createShader(
				{ nvrhi::ShaderType::Compute, "", "DDGIProbeBlendingCS" }, irrBlob->GetBufferPointer(), irrBlob->GetBufferSize());
			if (m_BlendIrradiancePass.m_ComputeShader)
			{
				m_BlendIrradiancePass.m_ComputePipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc().setComputeShader(m_BlendIrradiancePass.m_ComputeShader).addBindingLayout(m_BlendIrradiancePass.m_BindingLayout));
			}
			if (m_BlendIrradiancePass.m_ComputePipeline)
				m_BlendIrradiancePass.m_Initialized = true;
			else
				logger::error("DDGIPass::SetupBlendPasses - Failed to create irradiance blend shader or pipeline.");
		}
		else
		{
			logger::error("DDGIPass::SetupBlendPasses - Failed to compile DDGIProbeBlendIrradiance.hlsl");
		}

		// Blend Distance
		winrt::com_ptr<IDxcBlob> distBlob;
		ShaderUtils::CompileShader(distBlob, L"data/shaders/raytracing/DDGI/DDGIProbeBlendDistance.hlsl", commonDefines, ShaderStage::Compute, L"DDGIProbeBlendingCS");
		if (distBlob)
		{
			m_BlendDistancePass.m_ComputeShader = device->createShader(
				{ nvrhi::ShaderType::Compute, "", "DDGIProbeBlendingCS" }, distBlob->GetBufferPointer(), distBlob->GetBufferSize());
			if (m_BlendDistancePass.m_ComputeShader)
			{
				m_BlendDistancePass.m_ComputePipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc().setComputeShader(m_BlendDistancePass.m_ComputeShader).addBindingLayout(m_BlendDistancePass.m_BindingLayout));
			}
			if (m_BlendDistancePass.m_ComputePipeline)
				m_BlendDistancePass.m_Initialized = true;
			else
				logger::error("DDGIPass::SetupBlendPasses - Failed to create distance blend shader or pipeline.");
		}
		else
		{
			logger::error("DDGIPass::SetupBlendPasses - Failed to compile DDGIProbeBlendDistance.hlsl");
		}
	}

	void DDGIPass::SetupRelocationPass()
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::Compute;
		if (GetRenderer()->IsVulkan())
			layoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets()
				.setShaderResourceOffset(0)
				.setUnorderedAccessViewOffset(1));
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::PushConstants(1, sizeof(DDGIRootConstants)),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0), // t0: VolumeDesc
			nvrhi::BindingLayoutItem::Texture_UAV(0),           // u0: RayData
			nvrhi::BindingLayoutItem::Texture_UAV(1)            // u1: ProbeData
		};

		m_RelocationPass.m_BindingLayout = device->createBindingLayout(layoutDesc);

		eastl::vector<DxcDefine> commonDefines;
		winrt::com_ptr<IDxcBlob> blob;
		ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/DDGI/DDGIProbeRelocation.hlsl", commonDefines, ShaderStage::Compute, L"DDGIProbeRelocationCS");
		if (blob)
		{
			m_RelocationPass.m_ComputeShader = device->createShader(
				{ nvrhi::ShaderType::Compute, "", "DDGIProbeRelocationCS" }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (m_RelocationPass.m_ComputeShader)
				m_RelocationPass.m_ComputePipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc().setComputeShader(m_RelocationPass.m_ComputeShader).addBindingLayout(m_RelocationPass.m_BindingLayout));
			m_RelocationPass.m_Initialized = static_cast<bool>(m_RelocationPass.m_ComputePipeline);
			if (!m_RelocationPass.m_Initialized)
				logger::error("DDGIPass::SetupRelocationPass - Failed to create shader or compute pipeline.");
		}
		else
		{
			logger::error("DDGIPass::SetupRelocationPass - Failed to compile DDGIProbeRelocation.hlsl.");
		}
	}

	void DDGIPass::SetupClassificationPass()
	{
		auto device = GetRenderer()->GetDevice();

		m_ClassificationPass.m_BindingLayout = m_RelocationPass.m_BindingLayout;

		eastl::vector<DxcDefine> commonDefines;
		winrt::com_ptr<IDxcBlob> blob;
		ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/DDGI/DDGIProbeClassification.hlsl", commonDefines, ShaderStage::Compute, L"DDGIProbeClassificationCS");
		if (blob)
		{
			m_ClassificationPass.m_ComputeShader = device->createShader(
				{ nvrhi::ShaderType::Compute, "", "DDGIProbeClassificationCS" }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (m_ClassificationPass.m_ComputeShader)
				m_ClassificationPass.m_ComputePipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc().setComputeShader(m_ClassificationPass.m_ComputeShader).addBindingLayout(m_ClassificationPass.m_BindingLayout));
			m_ClassificationPass.m_Initialized = static_cast<bool>(m_ClassificationPass.m_ComputePipeline);
			if (!m_ClassificationPass.m_Initialized)
				logger::error("DDGIPass::SetupClassificationPass - Failed to create shader or compute pipeline.");
		}
		else
		{
			logger::error("DDGIPass::SetupClassificationPass - Failed to compile DDGIProbeClassification.hlsl.");
		}
	}

	void DDGIPass::SetupResolvePass()
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::Compute;
		layoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), // b0: Camera
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1), // b1: DDGI
			nvrhi::BindingLayoutItem::Texture_SRV(0),            // t0: Depth
			nvrhi::BindingLayoutItem::Texture_SRV(1),            // t1: NormalRoughness
			nvrhi::BindingLayoutItem::Texture_SRV(2),            // t2: ProbeIrradiance
			nvrhi::BindingLayoutItem::Texture_SRV(3),            // t3: ProbeDistance
			nvrhi::BindingLayoutItem::Texture_SRV(4),            // t4: ProbeData
			nvrhi::BindingLayoutItem::Texture_UAV(0),            // u0: DiffuseRadiance
			nvrhi::BindingLayoutItem::Sampler(0),                // s0: DefaultSampler
			nvrhi::BindingLayoutItem::Sampler(1)                 // s1: BilinearClampSampler
		};

		m_ResolvePass.m_BindingLayout = device->createBindingLayout(layoutDesc);

		auto defines = Util::Shader::GetDDGIDefines(Scene::GetSingleton()->m_Settings);
		auto dxcDefines = Util::Shader::GetDXCDefines(defines);
		winrt::com_ptr<IDxcBlob> blob;
		ShaderUtils::CompileShader(blob, L"data/shaders/raytracing/DDGI/DDGIResolve.hlsl", dxcDefines, ShaderStage::Compute);
		if (blob)
		{
			m_ResolvePass.m_ComputeShader = device->createShader(
				{ nvrhi::ShaderType::Compute, "", "Main" }, blob->GetBufferPointer(), blob->GetBufferSize());
			if (m_ResolvePass.m_ComputeShader)
				m_ResolvePass.m_ComputePipeline = device->createComputePipeline(
					nvrhi::ComputePipelineDesc().setComputeShader(m_ResolvePass.m_ComputeShader).addBindingLayout(m_ResolvePass.m_BindingLayout));
			m_ResolvePass.m_Initialized = static_cast<bool>(m_ResolvePass.m_ComputePipeline);
			if (!m_ResolvePass.m_Initialized)
				logger::error("DDGIPass::SetupResolvePass - Failed to create shader or compute pipeline.");
		}
		else
		{
			logger::error("DDGIPass::SetupResolvePass - Failed to compile DDGIResolve.hlsl");
		}
	}

	void DDGIPass::CheckBindings()
	{
		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_TracePass.m_BindingSets[currentSlot])
			return;

		auto* renderer = GetRenderer();
		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();
		auto* renderTargets = renderer->GetRenderTargets();
		auto& textureManager = renderer->RenderTargetManager();

		// Trace Binding Set
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
				nvrhi::BindingSetItem::ConstantBuffer(2, scene->GetFeatureBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(3, m_SceneTLAS->GetRaytracingBuffer()),
				nvrhi::BindingSetItem::Texture_UAV(0, m_RayData),
				nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
				nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(2, sceneGraph->GetLightBuffer()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetInstanceBuffer()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetMeshBuffer()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetTransformBuffer()),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(6, sceneGraph->GetInstanceLightList()),
				nvrhi::BindingSetItem::Texture_SRV(7, m_ProbeData),
				nvrhi::BindingSetItem::Texture_SRV(8, m_ProbeIrradiance),
				nvrhi::BindingSetItem::Texture_SRV(9, m_ProbeDistance),
				nvrhi::BindingSetItem::RawBuffer_SRV(10, sceneGraph->GetMeshSlotRemapBuffer()),
				nvrhi::BindingSetItem::RawBuffer_SRV(11, sceneGraph->GetPropertiesBuffer()),
				nvrhi::BindingSetItem::Texture_SRV(12, renderer->GetWaterDisplacementTexture()),
				nvrhi::BindingSetItem::Texture_SRV(13, scene->GetSkinDetailNormalTexture()),
				nvrhi::BindingSetItem::Texture_SRV(14, scene->GetProjNoiseTexture()),
				nvrhi::BindingSetItem::Texture_SRV(15, scene->GetFlowMapTexture()),
				nvrhi::BindingSetItem::Sampler(0, m_DefaultSampler),
				nvrhi::BindingSetItem::Sampler(1, m_ClampSampler),
				nvrhi::BindingSetItem::Sampler(2, m_PointWrapSampler),
				nvrhi::BindingSetItem::Sampler(3, m_BilinearClampSampler)
			};
			m_TracePass.m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_TracePass.m_BindingLayout);
		}

		// Blend Irradiance Binding Set
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::PushConstants(1, sizeof(DDGIRootConstants)),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_VolumeDescBuffer),
				nvrhi::BindingSetItem::Texture_UAV(0, m_RayData),
				nvrhi::BindingSetItem::Texture_UAV(1, m_ProbeIrradiance),
				nvrhi::BindingSetItem::Texture_UAV(2, m_ProbeData),
				nvrhi::BindingSetItem::Texture_UAV(3, m_ProbeVariability)
			};
			m_BlendIrradiancePass.m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_BlendIrradiancePass.m_BindingLayout);
		}

		// Blend Distance Binding Set
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::PushConstants(1, sizeof(DDGIRootConstants)),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_VolumeDescBuffer),
				nvrhi::BindingSetItem::Texture_UAV(0, m_RayData),
				nvrhi::BindingSetItem::Texture_UAV(1, m_ProbeDistance),
				nvrhi::BindingSetItem::Texture_UAV(2, m_ProbeData),
				nvrhi::BindingSetItem::Texture_UAV(3, m_ProbeVariability)
			};
			m_BlendDistancePass.m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_BlendDistancePass.m_BindingLayout);
		}

		// Relocation & Classification Binding Sets
		if (m_RelocationPass.m_BindingLayout)
		{
			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::PushConstants(1, sizeof(DDGIRootConstants)),
				nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_VolumeDescBuffer),
				nvrhi::BindingSetItem::Texture_UAV(0, m_RayData),
				nvrhi::BindingSetItem::Texture_UAV(1, m_ProbeData)
			};
			m_RelocationPass.m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_RelocationPass.m_BindingLayout);
			m_ClassificationPass.m_BindingSets[currentSlot] = m_RelocationPass.m_BindingSets[currentSlot];
		}

		// Resolve Binding Set
		if (m_ResolvePass.m_BindingLayout)
		{
			auto* diffuseTexture = textureManager.GetTexture(RenderTarget::DiffuseRadiance);
			auto* depthTexture = renderer->GetDepthTexture();
			auto normalTexture = renderTargets->normalRoughness;

			nvrhi::BindingSetDesc desc;
			desc.bindings = {
				nvrhi::BindingSetItem::ConstantBuffer(0, scene->GetCameraBuffer()),
				nvrhi::BindingSetItem::ConstantBuffer(1, m_ConstantBuffer),
				nvrhi::BindingSetItem::Texture_SRV(0, depthTexture),
				nvrhi::BindingSetItem::Texture_SRV(1, normalTexture),
				nvrhi::BindingSetItem::Texture_SRV(2, m_ProbeIrradiance),
				nvrhi::BindingSetItem::Texture_SRV(3, m_ProbeDistance),
				nvrhi::BindingSetItem::Texture_SRV(4, m_ProbeData),
				nvrhi::BindingSetItem::Texture_UAV(0, diffuseTexture),
				nvrhi::BindingSetItem::Sampler(0, m_DefaultSampler),
				nvrhi::BindingSetItem::Sampler(1, m_BilinearClampSampler)
			};
			m_ResolvePass.m_BindingSets[currentSlot] = renderer->GetDevice()->createBindingSet(desc, m_ResolvePass.m_BindingLayout);
		}

		m_BindingSetDirty[currentSlot] = false;
	}

	void DDGIPass::ClearCache(nvrhi::ICommandList* commandList)
	{
		if (m_ProbeIrradiance) commandList->clearTextureFloat(m_ProbeIrradiance, nvrhi::AllSubresources, nvrhi::Color(0.0f));
		if (m_ProbeDistance) commandList->clearTextureFloat(m_ProbeDistance, nvrhi::AllSubresources, nvrhi::Color(1e5f, 1e10f, 0.0f, 0.0f));
		if (m_ProbeData) commandList->clearTextureFloat(m_ProbeData, nvrhi::AllSubresources, nvrhi::Color(0.0f));
		if (m_RayData) commandList->clearTextureFloat(m_RayData, nvrhi::AllSubresources, nvrhi::Color(0.0f));

		m_ScrollOffsets = { 0, 0, 0 };
		m_AnchorPosition = Scene::GetSingleton()->GetCameraData()->Position;
		m_FrameCounter = 0;
		m_ResetCache = false;
	}

	void DDGIPass::UpdateVolumeDesc()
	{
		const auto& settings = Scene::GetSingleton()->m_Settings.DDGI;
		const auto* camera = Scene::GetSingleton()->GetCameraData();

		// Handle toroidal scrolling relative to camera movement
		float3 camPos = camera ? camera->Position : float3(0.0f);
		bool scrollClear[3] = { false, false, false };
		int scrollDirections[3] = { 0, 0, 0 };

		for (int axis = 0; axis < 3; axis++)
		{
			float spacing = (&m_ProbeSpacing.x)[axis];
			int& scrollOffset = (&m_ScrollOffsets.x)[axis];
			int probeCount = (&m_ProbeCounts.x)[axis];

			// Rebase whole-volume scrolls while preserving the effective origin.
			if (scrollOffset != 0 && scrollOffset % probeCount == 0)
			{
				(&m_AnchorPosition.x)[axis] += probeCount * spacing * (scrollOffset > 0 ? 1.0f : -1.0f);
				scrollOffset = 0;
			}

			const float effectiveOrigin = (&m_AnchorPosition.x)[axis] + scrollOffset * spacing;
			float axisDelta = (&camPos.x)[axis] - effectiveOrigin;
			if (fabsf(axisDelta) >= spacing)
			{
				int steps = static_cast<int>(axisDelta / spacing);
				scrollOffset += steps;
				scrollClear[axis] = true;
				scrollDirections[axis] = (steps > 0) ? 1 : 0;
			}
		}

		DDGIVolumeDescGPUPacked& packed = m_DDGIData->Volume;
		memset(&packed, 0, sizeof(packed));

		packed.origin = m_AnchorPosition;
		packed.probeHysteresis = settings.Hysteresis;
		packed.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };

		// Random probe ray rotation per frame
		const float angleX = (m_FrameCounter * 0.61803398875f) * DirectX::XM_2PI;
		const float angleY = (m_FrameCounter * 0.41421356237f) * DirectX::XM_2PI;
		packed.probeRayRotation = { sinf(angleX * 0.5f), cosf(angleX * 0.5f) * sinf(angleY * 0.5f), 0.0f, cosf(angleX * 0.5f) * cosf(angleY * 0.5f) };

		packed.probeMaxRayDistance = 1e5f;
		packed.probeNormalBias = settings.NormalBias;
		packed.probeViewBias = settings.ViewBias;
		packed.probeDistanceExponent = 50.0f;
		packed.probeIrradianceEncodingGamma = 5.0f;
		packed.probeIrradianceThreshold = 0.25f;
		packed.probeBrightnessThreshold = 0.10f;
		packed.probeMinFrontfaceDistance = 0.1f;
		packed.probeSpacing = m_ProbeSpacing;

		// packed0: probeCounts
		packed.packed0  = static_cast<uint32_t>(m_ProbeCounts.x);
		packed.packed0 |= static_cast<uint32_t>(m_ProbeCounts.y) << 10;
		packed.packed0 |= static_cast<uint32_t>(m_ProbeCounts.z) << 20;

		// packed1: thresholds
		packed.packed1  = static_cast<uint32_t>(0.1f * 65535.0f);
		packed.packed1 |= static_cast<uint32_t>(0.1f * 65535.0f) << 16;

		// packed2: counts
		packed.packed2  = static_cast<uint32_t>(m_RaysPerProbe);
		packed.packed2 |= static_cast<uint32_t>(6) << 16;
		packed.packed2 |= static_cast<uint32_t>(14) << 24;

		// packed3: probeScrollOffsets.x and .y
		packed.packed3 = (packed.packed3 & ~0x7FFF) | abs(m_ScrollOffsets.x);
		packed.packed3 = (packed.packed3 & ~0x8000) | ((m_ScrollOffsets.x < 0) << 15);
		packed.packed3 = (packed.packed3 & ~0x10000) | (abs(m_ScrollOffsets.y) << 16);
		packed.packed3 = (packed.packed3 & ~0x80000000) | ((m_ScrollOffsets.y < 0) << 31);

		// packed4: probeScrollOffsets.z and feature bits
		packed.packed4 = (packed.packed4 & ~0x7FFF) | abs(m_ScrollOffsets.z);
		packed.packed4 = (packed.packed4 & ~0x8000) | ((m_ScrollOffsets.z < 0) << 15);

		// Feature bits in packed4:
		packed.packed4 |= (1 << 16); // movementType = scrolling
		packed.packed4 |= (6 << 17); // probeRayDataFormat = F32x4
		packed.packed4 |= (6 << 20); // probeIrradianceFormat = F32x4
		if (settings.EnableRelocation) packed.packed4 |= (1 << 23);
		if (settings.EnableClassification) packed.packed4 |= (1 << 24);
		if (scrollClear[0]) packed.packed4 |= (1 << 26);
		if (scrollClear[1]) packed.packed4 |= (1 << 27);
		if (scrollClear[2]) packed.packed4 |= (1 << 28);
		if (scrollDirections[0]) packed.packed4 |= (1 << 29);
		if (scrollDirections[1]) packed.packed4 |= (1 << 30);
		if (scrollDirections[2]) packed.packed4 |= (1 << 31);
	}

	void DDGIPass::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		const auto revision = Scene::GetSingleton()->GetLightingRevision();
		if (m_ResetCache || m_LightingRevision != revision)
		{
			ClearCache(commandList);
			m_LightingRevision = revision;
		}

		UpdateVolumeDesc();
		m_FrameCounter++;

		commandList->writeBuffer(m_ConstantBuffer, m_DDGIData.get(), sizeof(DDGIData));
		commandList->writeBuffer(m_VolumeDescBuffer, &m_DDGIData->Volume, sizeof(DDGIVolumeDescGPUPacked));

		CheckBindings();

		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		// 1. Probe Trace Pass
		if (m_TracePass.m_ComputePipeline || m_TracePass.m_RayPipeline)
		{
			auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
			nvrhi::BindingSetVector bindings = {
				m_TracePass.m_BindingSets[currentSlot],
				sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetMaterialDescriptors()->m_DescriptorTable,
				sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
			};

			const int probesPerPlane = m_ProbeCounts.x * m_ProbeCounts.y;

			if (m_TracePass.m_RayPipeline)
			{
				nvrhi::rt::State state;
				state.shaderTable = m_TracePass.m_ShaderTable;
				state.bindings = bindings;
				commandList->setRayTracingState(state);

				nvrhi::rt::DispatchRaysArguments args;
				args.width = m_RaysPerProbe;
				args.height = probesPerPlane;
				args.depth = m_ProbeCounts.z;
				commandList->dispatchRays(args);
			}
			else if (m_TracePass.m_ComputePipeline)
			{
				nvrhi::ComputeState state;
				state.pipeline = m_TracePass.m_ComputePipeline;
				state.bindings = bindings;
				commandList->setComputeState(state);

				const uint groupX = Util::Math::DivideRoundUp(static_cast<uint>(m_RaysPerProbe), 8u);
				const uint groupY = Util::Math::DivideRoundUp(static_cast<uint>(probesPerPlane), 8u);
				commandList->dispatch(groupX, groupY, m_ProbeCounts.z);
			}
		}

		// 2. Probe Irradiance Blending
		if (m_BlendIrradiancePass.m_ComputePipeline && m_BlendIrradiancePass.m_BindingSets[currentSlot])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_BlendIrradiancePass.m_ComputePipeline;
			state.bindings = { m_BlendIrradiancePass.m_BindingSets[currentSlot] };
			commandList->setComputeState(state);
			commandList->setPushConstants(&m_DDGIRootConstants, sizeof(m_DDGIRootConstants));
			commandList->dispatch(m_ProbeCounts.x, m_ProbeCounts.y, m_ProbeCounts.z);
		}

		// 3. Probe Distance Blending
		if (m_BlendDistancePass.m_ComputePipeline && m_BlendDistancePass.m_BindingSets[currentSlot])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_BlendDistancePass.m_ComputePipeline;
			state.bindings = { m_BlendDistancePass.m_BindingSets[currentSlot] };
			commandList->setComputeState(state);
			commandList->setPushConstants(&m_DDGIRootConstants, sizeof(m_DDGIRootConstants));
			commandList->dispatch(m_ProbeCounts.x, m_ProbeCounts.y, m_ProbeCounts.z);
		}

		// 4. Probe Relocation (every 4 frames)
		if (Scene::GetSingleton()->m_Settings.DDGI.EnableRelocation && (m_FrameCounter % 4 == 0) &&
			m_RelocationPass.m_ComputePipeline && m_RelocationPass.m_BindingSets[currentSlot])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_RelocationPass.m_ComputePipeline;
			state.bindings = { m_RelocationPass.m_BindingSets[currentSlot] };
			commandList->setComputeState(state);
			commandList->setPushConstants(&m_DDGIRootConstants, sizeof(m_DDGIRootConstants));
			commandList->dispatch(Util::Math::DivideRoundUp(
				static_cast<uint>(m_ProbeCounts.x * m_ProbeCounts.y * m_ProbeCounts.z), 32u), 1, 1);
		}

		// 5. Probe Classification (every 8 frames)
		if (Scene::GetSingleton()->m_Settings.DDGI.EnableClassification && (m_FrameCounter % 8 == 0) &&
			m_ClassificationPass.m_ComputePipeline && m_ClassificationPass.m_BindingSets[currentSlot])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_ClassificationPass.m_ComputePipeline;
			state.bindings = { m_ClassificationPass.m_BindingSets[currentSlot] };
			commandList->setComputeState(state);
			commandList->setPushConstants(&m_DDGIRootConstants, sizeof(m_DDGIRootConstants));
			commandList->dispatch(Util::Math::DivideRoundUp(
				static_cast<uint>(m_ProbeCounts.x * m_ProbeCounts.y * m_ProbeCounts.z), 32u), 1, 1);
		}

		// 6. Screen-Space Resolve Pass (samples DDGI irradiance into DiffuseRadiance)
		if (m_ResolvePass.m_ComputePipeline && m_ResolvePass.m_BindingSets[currentSlot])
		{
			nvrhi::ComputeState state;
			state.pipeline = m_ResolvePass.m_ComputePipeline;
			state.bindings = { m_ResolvePass.m_BindingSets[currentSlot] };
			commandList->setComputeState(state);

			const auto renderSize = Renderer::GetSingleton()->GetResolution();
			commandList->dispatch(
				Util::Math::DivideRoundUp(renderSize.x, 8u),
				Util::Math::DivideRoundUp(renderSize.y, 8u));
		}
	}
}
