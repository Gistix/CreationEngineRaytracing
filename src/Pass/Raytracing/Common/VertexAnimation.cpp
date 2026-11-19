#include "VertexAnimation.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Core/MeshManager.h"

namespace Pass
{
	VertexAnimation::VertexAnimation(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_Buffer = Util::CreateStructuredRingBuffer<VertexAnimationData>(renderer->GetDevice(), MAX_MESHES, "Vertex Animation Buffer");
		m_GrassBuffer = Util::CreateStructuredRingBuffer<GrassWindData>(renderer->GetDevice(), MAX_GRASS_GROUPS, "Grass Wind Buffer");
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
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void VertexAnimation::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto makePipeline = [&](const wchar_t* entryPointW, const char* entryPoint, nvrhi::ShaderHandle& shader, nvrhi::ComputePipelineHandle& pipeline) {
			winrt::com_ptr<IDxcBlob> shaderBlob;
			ShaderUtils::CompileShader(shaderBlob, L"data/shaders/VertexAnimation.hlsl", {}, ShaderStage::Compute, entryPointW);
			if (!shaderBlob)
				return;

			shader = device->createShader({ nvrhi::ShaderType::Compute, "", entryPoint }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
			if (!shader)
				return;

			auto pipelineDesc = nvrhi::ComputePipelineDesc()
				.setComputeShader(shader)
				.addBindingLayout(m_BindingLayout)
				.addBindingLayout(sceneGraph->GetVertexCopyDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetDynamicVertexReadDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetAnimatedPositionWriteDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetVertexWriteDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetPrevPositionWriteDescriptors()->m_Layout)
				.addBindingLayout(sceneGraph->GetDynamicVertexWriteDescriptors()->m_Layout);

			pipeline = device->createComputePipeline(pipelineDesc);
		};

		makePipeline(L"Main", "Main", m_ComputeShader, m_ComputePipeline);
		makePipeline(L"GrassMain", "GrassMain", m_GrassComputeShader, m_GrassComputePipeline);
	}

	void VertexAnimation::QueueUpdate(BaseMesh* mesh, const VertexAnimationData& data)
	{
		std::scoped_lock lock(m_QueueMutex);
		m_QueuedMeshes[mesh] = data;
	}

	void VertexAnimation::QueueGrassUpdate(BaseMesh* mesh, const GrassWindData& data)
	{
		std::scoped_lock lock(m_QueueMutex);
		m_QueuedGrass[mesh] = data;
	}

	bool VertexAnimation::PrepareMeshes(nvrhi::ICommandList* commandList, uint32_t& numMeshes, uint32_t& numVertices)
	{
		for (auto& [mesh, data] : m_QueuedMeshes) {
			if (numMeshes >= MAX_MESHES) {
				logger::critical("VertexAnimation::PrepareMeshes - Exceeded maximum mesh update limit of {}", MAX_MESHES);
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

	bool VertexAnimation::PrepareGrass(nvrhi::ICommandList* commandList, uint32_t& numGroups, uint32_t& numVertices)
	{
		for (auto& [mesh, data] : m_QueuedGrass) {
			if (numGroups >= MAX_GRASS_GROUPS) {
				logger::critical("VertexAnimation::PrepareGrass - Exceeded maximum grass group update limit of {}", MAX_GRASS_GROUPS);
				break;
			}

			m_GrassData[numGroups++] = data;
			numVertices = std::max(numVertices, data.InstanceCount * data.VertexCount);
		}
		m_QueuedGrass.clear();

		if (numGroups == 0)
			return false;

		commandList->writeBuffer(m_GrassBuffer.current(), m_GrassData.data(), sizeof(GrassWindData) * numGroups);
		return true;
	}

	void VertexAnimation::CheckBindings()
	{
		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (m_BindingSets[currentSlot])
			return;

		auto* meshManager = Scene::GetSingleton()->GetSceneGraph()->GetMeshManager().get();

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_Buffer[currentSlot]),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_GrassBuffer[currentSlot]),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, meshManager->GetInstanceTransformBuffer())
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);
	}

	void VertexAnimation::Execute(nvrhi::ICommandList* commandList)
	{
		if (!m_ComputePipeline || !m_GrassComputePipeline)
			return;

		CheckBindings();

		uint32_t numMeshes = 0;
		uint32_t meshVertexCount = 0;
		uint32_t numGroups = 0;
		uint32_t grassVertexCount = 0;

		bool hasMeshes = false;
		bool hasGrass = false;
		{
			std::scoped_lock lock(m_QueueMutex);
			hasMeshes = PrepareMeshes(commandList, numMeshes, meshVertexCount);
			hasGrass = PrepareGrass(commandList, numGroups, grassVertexCount);
		}

		if (!hasMeshes && !hasGrass)
			return;

		const uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSets[currentSlot],
			sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
			sceneGraph->GetDynamicVertexReadDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetAnimatedPositionWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetDynamicVertexWriteDescriptors()->m_DescriptorTable
		};

		nvrhi::ComputeState state;
		state.bindings = bindings;

		if (hasMeshes) {
			state.pipeline = m_ComputePipeline;
			commandList->setComputeState(state);
			commandList->dispatch(numMeshes, Util::Math::DivideRoundUp(meshVertexCount, 32u));
		}

		// Grass groups write disjoint dynamic buffers, so no barrier is needed against the mesh dispatch.
		if (hasGrass) {
			state.pipeline = m_GrassComputePipeline;
			commandList->setComputeState(state);
			commandList->dispatch(Util::Math::DivideRoundUp(grassVertexCount, 64u), numGroups);
		}
	}
}
