#include "DisplacementMM.h"

#include "Renderer.h"
#include "ShaderUtils.h"

#include "Core/Material.h"
#include "Scene.h"
#include "Util.h"

void DisplacementMM::Initialize()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	m_LinearWrapSampler = device->createSampler(
		nvrhi::SamplerDesc()
		.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
		.setAllFilters(true));

	m_DMMData = eastl::make_unique<DisplacementMMData>();
	m_DMMBuffer = device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
		sizeof(DisplacementMM), "DMM Data", Constants::MAX_CB_VERSIONS));

	nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
	globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
	globalBindingLayoutDesc.bindings = {
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
		nvrhi::BindingLayoutItem::Sampler(0),
		nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
		nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1),
		nvrhi::BindingLayoutItem::RawBuffer_UAV(2)
	};

	m_BindingLayout = device->createBindingLayout(globalBindingLayoutDesc);

	m_Generate.Create(m_BindingLayout, 0);
	m_MinMax.Create(m_BindingLayout, 1);
	m_Pack.Create(m_BindingLayout, 2);
}

void DisplacementMM::Pipeline::Create(nvrhi::BindingLayoutHandle bindingLayout, int index)
{
	auto device = Renderer::GetSingleton()->GetDevice();

	winrt::com_ptr<IDxcBlob> shaderBlob;
	ShaderUtils::CompileShader(shaderBlob, L"data/shaders/DMM.hlsl", {}, L"cs_6_5", index == 0 ? L"CS_Generate" : (index == 1 ? L"CS_MinMax" : L"CS_NormalizePack" ));
	m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", index == 0 ? "CS_Generate" : (index == 1 ? "CS_MinMax" : "CS_NormalizePack") }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

	if (!m_ComputeShader)
		return;

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	auto pipelineDesc = nvrhi::ComputePipelineDesc()
		.setComputeShader(m_ComputeShader)
		.addBindingLayout(bindingLayout)
		.addBindingLayout(sceneGraph->GetTriangleDescriptors()->m_Layout)
		.addBindingLayout(sceneGraph->GetVertexDescriptors()->m_Layout)
		.addBindingLayout(sceneGraph->GetTextureDescriptors()->m_Layout);

	m_ComputePipeline = device->createComputePipeline(pipelineDesc);
}

nvrhi::IBuffer* DisplacementMM::GetMicroValuesBuffer(uint elements)
{
	if (!m_MicroValuesBuffer || elements > m_MicroValuesElements) {
		m_MicroValuesBuffer = Util::CreateStructuredBuffer<float>(Renderer::GetSingleton()->GetDevice(), elements, "MicroValues Buffer", true, nvrhi::ResourceStates::Common);
		m_MicroValuesElements = elements;
	}

	return m_MicroValuesBuffer;
}

nvrhi::IBuffer* DisplacementMM::GetBiasScaleBuffer(uint elements)
{
	if (!m_BiasScaleBuffer || elements > m_BiasScaleElements) {
		m_BiasScaleBuffer = Util::CreateStructuredBuffer<float2>(Renderer::GetSingleton()->GetDevice(), elements, "BiasScale Buffer", true, nvrhi::ResourceStates::Common);
		m_BiasScaleElements = elements;
	}

	return m_BiasScaleBuffer;
}

void DisplacementMM::ProcessMesh(nvrhi::ICommandList* commandList, Mesh* mesh)
{
	if (mesh->flags.none(Mesh::Flags::Displacement))
		return;

	auto device = Renderer::GetSingleton()->GetDevice();

	const auto& material = mesh->material;

	auto subdivisionLevel = 5;

	m_DMMData->TriangleCount = mesh->triangleCount;
	m_DMMData->SubdivisionLevel = subdivisionLevel;
	m_DMMData->MicroVertexCount = (subdivisionLevel + 1) * (subdivisionLevel + 2) / 2;
	m_DMMData->PackedStride = PACKED_STRIDE;
	m_DMMData->MeshIndex = mesh->m_DescriptorHandle.Get();
	m_DMMData->DisplacementIndex = material.GetDisplacementDescriptorIndex();

	// Assign DMM Usage Count
	{
		NVAPI_D3D12_RAYTRACING_DISPLACEMENT_MICROMAP_USAGE_COUNT uc{};
		uc.count = mesh->triangleCount;
		uc.subdivisionLevel = subdivisionLevel;
		uc.format = NVAPI_D3D12_RAYTRACING_DISPLACEMENT_MICROMAP_FORMAT_DC1_64_TRIS_64_BYTES;

		mesh->dmm.usageCount = uc;
	}

	// Buffer Size
	auto microValuesElements = m_DMMData->TriangleCount * m_DMMData->MicroVertexCount;
	auto biasScaleElements = m_DMMData->TriangleCount;

	nvrhi::BindingSetDesc bindingSetDesc;
	bindingSetDesc.bindings = {
		nvrhi::BindingSetItem::ConstantBuffer(0, m_DMMBuffer),
		nvrhi::BindingSetItem::Sampler(0, m_LinearWrapSampler),
		nvrhi::BindingSetItem::StructuredBuffer_UAV(0, GetMicroValuesBuffer(microValuesElements)),
		nvrhi::BindingSetItem::StructuredBuffer_UAV(1, GetBiasScaleBuffer(biasScaleElements)),
		nvrhi::BindingSetItem::RawBuffer_UAV(2, mesh->dmm.buffer)
	};

	m_BindingSet = device->createBindingSet(bindingSetDesc, m_BindingLayout);

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	nvrhi::BindingSetVector bindings = {
		m_BindingSet,
		sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->GetDescriptorTable(),
		sceneGraph->GetVertexDescriptors()->m_DescriptorTable,
		sceneGraph->GetTextureDescriptors()->m_DescriptorTable->GetDescriptorTable()
	};

	commandList->writeBuffer(m_DMMBuffer, m_DMMData.get(), sizeof(DisplacementMMData));

	auto threadGroupSize = Util::Math::DivideRoundUp(mesh->triangleCount, 64u);

	// Generate
	{
		nvrhi::ComputeState state;
		state.pipeline = m_Generate.m_ComputePipeline;
		state.bindings = bindings;
		commandList->setComputeState(state);

		commandList->dispatch(threadGroupSize);
	}

	// Generate
	{
		nvrhi::ComputeState state;
		state.pipeline = m_MinMax.m_ComputePipeline;
		state.bindings = bindings;
		commandList->setComputeState(state);

		commandList->dispatch(threadGroupSize);
	}

	// Generate
	{
		nvrhi::ComputeState state;
		state.pipeline = m_Pack.m_ComputePipeline;
		state.bindings = bindings;
		commandList->setComputeState(state);

		commandList->dispatch(threadGroupSize);
	}
}
