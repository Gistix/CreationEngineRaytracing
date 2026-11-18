#include "VertexAnimation.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"

namespace Pass
{
	VertexAnimation::VertexAnimation(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_Buffer = Util::CreateStructuredRingBuffer<VertexAnimationData>(renderer->GetDevice(), MAX_MESHES, "Vertex Animation Buffer");
	}

	void VertexAnimation::Initialize()
	{
		CreateBindingLayout();
		CreatePipeline();
	}

	void VertexAnimation::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void VertexAnimation::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/VertexAnimation.hlsl", {}, ShaderStage::Compute);
		if (!shaderBlob)
			return;

		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetVertexCopyDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetDynamicVertexReadDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetAnimatedPositionWriteDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexWriteDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetPrevPositionWriteDescriptors()->m_Layout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void VertexAnimation::QueueUpdate(BaseMesh* mesh, const VertexAnimationData& data)
	{
		std::scoped_lock lock(m_QueueMutex);
		m_QueuedMeshes[mesh] = data;
	}

	bool VertexAnimation::PrepareResources(nvrhi::ICommandList* commandList, uint32_t& numMeshes, uint32_t& numVertices)
	{
		std::scoped_lock lock(m_QueueMutex);

		for (auto& [mesh, data] : m_QueuedMeshes) {
			if (numMeshes >= MAX_MESHES) {
				logger::critical("VertexAnimation::PrepareResources - Exceeded maximum mesh update limit of {}", MAX_MESHES);
				break;
			}

			m_AnimationData[numMeshes++] = data;
			numVertices = std::max(numVertices, data.VertexCount);
		}
		m_QueuedMeshes.clear();

		if (numMeshes == 0)
			return false;

		commandList->writeBuffer(m_Buffer.current(), m_AnimationData.data(), sizeof(VertexAnimationData) * numMeshes);

		return true;
	}

	void VertexAnimation::CheckBindings()
	{
		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (m_BindingSets[currentSlot])
			return;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_Buffer[currentSlot])
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
	}

	void VertexAnimation::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_ComputePipeline)
			return;

		CheckBindings();

		uint32_t numMeshes = 0;
		uint32_t vertexCount = 0;

		if (!PrepareResources(commandList, numMeshes, vertexCount))
			return;

		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSets[currentSlot],
			sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
			sceneGraph->GetDynamicVertexReadDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetAnimatedPositionWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable
		};

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = bindings;
		commandList->setComputeState(state);

		const auto vertexGroups = Util::Math::DivideRoundUp(vertexCount, 32u);
		commandList->dispatch(numMeshes, vertexGroups);
	}
}
