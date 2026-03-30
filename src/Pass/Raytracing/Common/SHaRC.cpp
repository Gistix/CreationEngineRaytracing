#include "SHaRC.h"
#include "Renderer.h"
#include "Scene.h"

#include "Interop/SharcTypes.h"

namespace Pass
{
	SHaRC::SHaRC(Renderer* renderer, SceneTLAS* sceneTLAS)
		: RenderPass(renderer), m_SceneTLAS(sceneTLAS)
	{
		m_LinearWrapSampler = GetRenderer()->GetDevice()->createSampler(
			nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true));

		m_SHaRCData = eastl::make_unique<SHaRCData>();

		m_SHaRCBuffer = renderer->GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
			sizeof(SHaRCData), "SHaRC Data", Constants::MAX_CB_VERSIONS));

		SettingsChanged(Scene::GetSingleton()->m_Settings);

		auto device = renderer->GetDevice();

		m_HashEntriesBuffer = Util::CreateStructuredBuffer<uint64_t>(device, MAX_CAPACITY, "SHaRC Hash Entries Buffer", true);
		m_LockBuffer = Util::CreateStructuredBuffer<uint>(device, MAX_CAPACITY, "SHaRC Lock Buffer", true);
		m_AccumulationBuffer = Util::CreateStructuredBuffer<SharcAccumulationData>(device, MAX_CAPACITY, "SHaRC Accumulation Buffer", true);
		m_ResolveBuffer = Util::CreateStructuredBuffer<SharcPackedData>(device, MAX_CAPACITY, "SHaRC Resolve Buffer", true);

		m_Defines = Util::Shader::GetRaytracingDefines(Scene::GetSingleton()->m_Settings, true, true);
		m_SceneTLAS->GetTopLevelAS().AddListener(this);

		SetupUpdate();
		SetupResolve();
	}

	void SHaRC::SettingsChanged(const Settings& settings)
	{
		m_Enabled = settings.SHaRCSettings.Enabled;
		m_SHaRCData->SceneScale = settings.SHaRCSettings.SceneScale / Util::Units::GAME_UNIT_TO_M;
		m_SHaRCData->AccumFrameNum = static_cast<uint>(settings.SHaRCSettings.AccumFrameNum);
		m_SHaRCData->StaleFrameNum = static_cast<uint>(settings.SHaRCSettings.StaleFrameNum);
		m_SHaRCData->RadianceScale = settings.SHaRCSettings.RadianceScale;
		m_SHaRCData->AntifireflyFilter = settings.SHaRCSettings.AntifireflyFilter ? 1 : 0;

		auto defines = Util::Shader::GetRaytracingDefines(settings, m_Enabled, true);

		if (defines != m_Defines) {
			m_Defines = defines;
			SetupUpdate();
			m_DirtyBindings = true;
		}
	}

	void SHaRC::SetupUpdate()
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = GetRenderer()->m_Settings.UseRayQuery ? nvrhi::ShaderType::Compute : nvrhi::ShaderType::AllRayTracing;
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
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6),
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

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/raytracing/PathTracing/RayGeneration.hlsl", defines, L"cs_6_5");
		m_UpdatePass.m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "SHaRC Update Shader", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		auto* sceneGraph = scene->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_UpdatePass.m_ComputeShader)
			.addBindingLayout(m_UpdatePass.m_BindingLayout)
			.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetPrevPositionDescriptors()->m_Layout);

		m_UpdatePass.m_ComputePipeline = GetRenderer()->GetDevice()->createComputePipeline(pipelineDesc);
	}

	void SHaRC::SetupResolve()
	{
		auto device = GetRenderer()->GetDevice();

		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::All;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2)
		};

		m_ResolvePass.m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);


		const auto linearBlockSizeWStr = std::to_wstring(RESOLVE_LINEAR_BLOCK_SIZE);

		eastl::vector<DxcDefine> defines = {
			{ L"LINEAR_BLOCK_SIZE", linearBlockSizeWStr.c_str() },
			{ L"SHARC", L"" },
			{ L"SHARC_UPDATE", L"0" },
			{ L"SHARC_RESOLVE", L"1" }
		};

		winrt::com_ptr<IDxcBlob> rayGenBlob;
		ShaderUtils::CompileShader(rayGenBlob, L"data/shaders/SharcResolve.hlsl", defines, L"cs_6_5");
		m_ResolvePass.m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "SHaRC Resolve Shader", "Main" }, rayGenBlob->GetBufferPointer(), rayGenBlob->GetBufferSize());

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ResolvePass.m_ComputeShader)
			.addBindingLayout(m_ResolvePass.m_BindingLayout);

		m_ResolvePass.m_ComputePipeline = GetRenderer()->GetDevice()->createComputePipeline(pipelineDesc);

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::ConstantBuffer(0, Scene::GetSingleton()->GetCameraBuffer()),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_SHaRCBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_HashEntriesBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_AccumulationBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_ResolveBuffer),
		};

		m_ResolvePass.m_BindingSet = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_ResolvePass.m_BindingLayout);
	}

	void SHaRC::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		auto* scene = Scene::GetSingleton();

		auto* sceneGraph = scene->GetSceneGraph();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
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
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_HashEntriesBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(1, m_LockBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_AccumulationBuffer)
		};

		m_UpdatePass.m_BindingSet = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_UpdatePass.m_BindingLayout);

		m_DirtyBindings = false;
	}

	void SHaRC::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_Enabled)
			return;

		commandList->writeBuffer(m_SHaRCBuffer, m_SHaRCData.get(), sizeof(SHaRCData));

		// Update Pass
		{
			auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

			nvrhi::ComputeState state;
			state.pipeline = m_UpdatePass.m_ComputePipeline;

			CheckBindings();

			state.bindings = {
				m_UpdatePass.m_BindingSet,
				sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetVertexDescriptors()->m_DescriptorTable,
				sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable(),
				sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable
			};
			commandList->setComputeState(state);

			uint2 resolution = Renderer::GetSingleton()->GetResolution();

			uint2 sharcRes = {
				Util::Math::DivideRoundUp(resolution.x, 5u),
				Util::Math::DivideRoundUp(resolution.y, 5u)
			};

			auto threadGroupSize = Util::Math::GetDispatchCount(sharcRes, 16);

			commandList->dispatch(threadGroupSize.x, threadGroupSize.y);
		}

		// Resolve Pass
		{
			nvrhi::ComputeState state;
			state.pipeline = m_ResolvePass.m_ComputePipeline;
			state.bindings = { m_ResolvePass.m_BindingSet };
			commandList->setComputeState(state);

			const auto threadGroupSize = Util::Math::DivideRoundUp(MAX_CAPACITY, RESOLVE_LINEAR_BLOCK_SIZE);

			commandList->dispatch(threadGroupSize);
		}
	}
}