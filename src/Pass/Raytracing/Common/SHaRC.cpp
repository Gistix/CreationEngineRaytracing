#include "SHaRC.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShaderCache.h"

#include "Interop/SharcTypes.h"

namespace Pass
{
	SHaRC::SHaRC(Renderer* renderer, SceneTLAS* sceneTLAS)
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

		m_SHaRCData = eastl::make_unique<SHaRCData>();

		m_SHaRCBuffer = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(SHaRCData), "SHaRC Data", Constants::MAX_CB_VERSIONS));

		m_HashEntriesBuffer = Util::CreateStructuredBuffer<uint64_t>(device, MAX_CAPACITY, "SHaRC Hash Entries Buffer", true);
		m_LockBuffer = Util::CreateStructuredBuffer<uint>(device, MAX_CAPACITY, "SHaRC Lock Buffer", true);
		m_AccumulationBuffer = Util::CreateStructuredBuffer<SharcAccumulationData>(device, MAX_CAPACITY, "SHaRC Accumulation Buffer", true);
		m_ResolveBuffer = Util::CreateStructuredBuffer<SharcPackedData>(device, MAX_CAPACITY, "SHaRC Resolve Buffer", true);

		m_SceneTLAS->GetTopLevelAS().AddListener(this);
	}

	void SHaRC::SettingsChanged(const Settings& settings)
	{
		const bool wasEnabled = m_Enabled;
		const float sceneScale = settings.SHaRCSettings.SceneScale / Util::Units::GAME_UNIT_TO_M;
		const uint accumFrameNum = static_cast<uint>(settings.SHaRCSettings.AccumFrameNum);
		const uint staleFrameNum = static_cast<uint>(settings.SHaRCSettings.StaleFrameNum);
		const float radianceScale = settings.SHaRCSettings.RadianceScale;

		const bool cacheSettingsChanged =
			m_SHaRCData->SceneScale != sceneScale ||
			m_SHaRCData->AccumFrameNum != accumFrameNum ||
			m_SHaRCData->StaleFrameNum != staleFrameNum ||
			m_SHaRCData->RadianceScale != radianceScale;

		m_Enabled = settings.SHaRCSettings.Enabled;
		m_SHaRCData->SceneScale = sceneScale;
		m_SHaRCData->AccumFrameNum = accumFrameNum;
		m_SHaRCData->StaleFrameNum = staleFrameNum;
		m_SHaRCData->RadianceScale = radianceScale;

		auto defines = Util::Shader::GetPathTracingDefines(settings, true, true);
		const bool definesChanged = defines != m_Defines;

		m_Defines = defines;

		if (m_Enabled && (!wasEnabled || definesChanged)) {
			SetupUpdate();
			SetupResolve();
			m_BindingSetDirty.fill(true);
		}

		if (m_Enabled && (!wasEnabled || cacheSettingsChanged || definesChanged))
			m_ResetCache = true;
	}

	void SHaRC::SetupUpdate()
	{
		auto device = GetRenderer()->GetDevice();
		m_UpdatePass.m_ComputeShader = nullptr;
		m_UpdatePass.m_ComputePipeline = nullptr;

		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = GetRenderer()->m_Settings.UseRayQuery ? nvrhi::ShaderType::Compute : nvrhi::ShaderType::AllRayTracing;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::Sampler(0),
			nvrhi::BindingLayoutItem::Sampler(1),
			nvrhi::BindingLayoutItem::Sampler(2),
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
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6),
			nvrhi::BindingLayoutItem::Texture_SRV(8),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2)
		};

		m_UpdatePass.m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);

		auto* scene = Scene::GetSingleton();

		auto defines = Util::Shader::GetDXCDefines(m_Defines);

		const auto threadGroupSizeWStr = std::to_wstring(UPDATE_THREAD_GROUP_SIZE);
		defines.emplace_back(L"THREAD_GROUP_SIZE", threadGroupSizeWStr.c_str());

		defines.emplace_back(L"USE_RAY_QUERY", L"1");

		auto* rayGenBlob = ShaderCache::GetShader(L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines, L"cs_6_5");
		if (!rayGenBlob) {
			logger::error("SHaRC::SetupUpdate - Failed to compile update shader.");
			return;
		}

		m_UpdatePass.m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "SHaRC Update Shader", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());
		if (!m_UpdatePass.m_ComputeShader) {
			logger::error("SHaRC::SetupUpdate - Failed to create update shader.");
			return;
		}

		auto* sceneGraph = scene->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_UpdatePass.m_ComputeShader)
			.addBindingLayout(m_UpdatePass.m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetMaterialDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetPrevPositionDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetCubemapDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout);

		m_UpdatePass.m_ComputePipeline = GetRenderer()->GetDevice()->createComputePipeline(pipelineDesc);
		if (!m_UpdatePass.m_ComputePipeline) {
			logger::error("SHaRC::SetupUpdate - Failed to create update pipeline.");
		}
	}

	void SHaRC::SetupResolve()
	{
		if (m_ResolvePass.m_Initialized)
			return;

		auto device = GetRenderer()->GetDevice();
		m_ResolvePass.m_ComputeShader = nullptr;
		m_ResolvePass.m_ComputePipeline = nullptr;

		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2)
		};

		m_ResolvePass.m_BindingLayout = device->createBindingLayout(globalBindingLayoutDesc);


		const auto linearBlockSizeWStr = std::to_wstring(RESOLVE_LINEAR_BLOCK_SIZE);

		eastl::vector<DxcDefine> defines = {
			{ L"LINEAR_BLOCK_SIZE", linearBlockSizeWStr.c_str() },
			{ L"SHARC", L"" },
			{ L"SHARC_UPDATE", L"0" },
			{ L"SHARC_RESOLVE", L"1" },
			{ L"SHARC_ENABLE_FADE_ACCELERATION", L"1" }
		};

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/SharcResolve.hlsl", defines, L"cs_6_5");
		if (!rayGenBlob) {
			logger::error("SHaRC::SetupResolve - Failed to compile resolve shader.");
			return;
		}

		m_ResolvePass.m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "SHaRC Resolve Shader", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());
		if (!m_ResolvePass.m_ComputeShader) {
			logger::error("SHaRC::SetupResolve - Failed to create resolve shader.");
			return;
		}

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ResolvePass.m_ComputeShader)
			.addBindingLayout(m_ResolvePass.m_BindingLayout);

		m_ResolvePass.m_ComputePipeline = device->createComputePipeline(pipelineDesc);
		if (!m_ResolvePass.m_ComputePipeline) {
			logger::error("SHaRC::SetupResolve - Failed to create resolve pipeline.");
			return;
		}

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, Scene::GetSingleton()->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SHaRCBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_HashEntriesBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_AccumulationBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_ResolveBuffer),
		};

		auto resolveBindingSet = device->createBindingSet(bindingSetDesc, m_ResolvePass.m_BindingLayout);
		for (uint32_t i = 0; i < Constants::MAX_FRAMES_IN_FLIGHT; i++)
			m_ResolvePass.m_BindingSets[i] = resolveBindingSet;

		m_ResolvePass.m_Initialized = true;
	}

	void SHaRC::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_UpdatePass.m_BindingSets[currentSlot])
			return;

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
			nvrhi::BindingSetItem::ConstantBuffer(3, m_SHaRCBuffer),
			nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_SceneTLAS->GetTopLevelAS().GetHandle()),
			nvrhi::BindingSetItem::Texture_SRV(1, scene->GetSkyHemiTexture()),
			nvrhi::BindingSetItem::Texture_SRV(2, scene->GetFlowMapTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(3, sceneGraph->GetLightBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(4, sceneGraph->GetInstanceBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(5, sceneGraph->GetMeshBuffer()),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(6, m_ResolveBuffer),
			nvrhi::BindingSetItem::Texture_SRV(8, scene->GetSkinDetailNormalTexture()),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_HashEntriesBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_LockBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_AccumulationBuffer)
		};

		m_UpdatePass.m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_UpdatePass.m_BindingLayout);

		m_BindingSetDirty[currentSlot] = false;
	}

	void SHaRC::ClearCache(nvrhi::ICommandList* commandList)
	{
		commandList->clearBufferUInt(m_HashEntriesBuffer, 0);
		commandList->clearBufferUInt(m_LockBuffer, 0);
		commandList->clearBufferUInt(m_AccumulationBuffer, 0);
		commandList->clearBufferUInt(m_ResolveBuffer, 0);
		m_FrameCounter = 0;
		m_ResetCache = false;
	}

	void SHaRC::Execute(nvrhi::ICommandList* commandList)
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		if (!m_Enabled)
			return;

		if (m_ResetCache)
			ClearCache(commandList);

		m_SHaRCData->FrameIndex = m_FrameCounter++;

		commandList->writeBuffer(m_SHaRCBuffer, m_SHaRCData.get(), sizeof(SHaRCData));

		// Update Pass
		{
			auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

			if (!m_UpdatePass.m_ComputePipeline)
				return;

			nvrhi::ComputeState state;
			state.pipeline = m_UpdatePass.m_ComputePipeline;

			CheckBindings();

			state.bindings = {
				m_UpdatePass.m_BindingSets[currentSlot],
				sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetMaterialDescriptors()->m_DescriptorTable,
				sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
				sceneGraph->GetCubemapDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable
			};
			commandList->setComputeState(state);

			uint2 resolution = Renderer::GetSingleton()->GetResolution();

			uint2 sharcRes = {
				Util::Math::DivideRoundUp(resolution.x, 5u),
				Util::Math::DivideRoundUp(resolution.y, 5u)
			};

			auto threadGroupSize = Util::Math::GetDispatchCount(sharcRes, Constants::PT_DISPATCH_THREADS);
			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}

		// Resolve Pass
		{
			if (!m_ResolvePass.m_ComputePipeline)
				return;

			nvrhi::ComputeState state;
			state.pipeline = m_ResolvePass.m_ComputePipeline;

			if (!m_ResolvePass.m_BindingSets[currentSlot]) {
				nvrhi::BindingSetDesc bindingSetDesc;
				bindingSetDesc.bindings = {
					nvrhi::BindingSetItem::ConstantBuffer(0, Scene::GetSingleton()->GetCameraBuffer()),
					nvrhi::BindingSetItem::ConstantBuffer(1, m_SHaRCBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_HashEntriesBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_AccumulationBuffer),
					nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_ResolveBuffer),
				};
				m_ResolvePass.m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_ResolvePass.m_BindingLayout);
			}

			state.bindings = { m_ResolvePass.m_BindingSets[currentSlot] };
			commandList->setComputeState(state);

			const auto threadGroupSize = Util::Math::DivideRoundUp(MAX_CAPACITY, RESOLVE_LINEAR_BLOCK_SIZE);

			commandList->dispatch(threadGroupSize);
		}
	}
}
