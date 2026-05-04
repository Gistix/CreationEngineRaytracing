#include "LandLODOccluder.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"

namespace Pass
{
	LandLODOccluder::LandLODOccluder(Renderer* renderer)
		: RenderPass(renderer)
	{
		m_Buffer = Util::CreateStructuredBuffer<LandLODUpdate>(renderer->GetDevice(), MAX_MESHES, "Land LOD Update Buffer");

		CreateBindingLayout();
		CreatePipeline();
	}

	void LandLODOccluder::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float4)),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void LandLODOccluder::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/LandLODOccluder.hlsl", {}, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetVertexCopyDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexWriteDescriptors()->m_Layout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	bool LandLODOccluder::PrepareResources(nvrhi::ICommandList* commandList, uint32_t& numMeshes, uint32_t& numVertices)
	{
		uint32_t meshIndex = 0;

		// TODO: Make updates lazy
		const auto& terrainLODInstances = Scene::GetSingleton()->GetSceneGraph()->GetTerrainLodInstances();
		for (auto& [block, blockRefr] : terrainLODInstances) {
			if (block->node->GetLODLevel() != 4)
				continue;

			for (auto& instance : blockRefr.instances)
			{
				auto firstMeshIndex = meshIndex;

				for (auto& mesh : instance->model->meshes) {
					if (meshIndex >= MAX_MESHES - 1) {
						logger::critical("LandLODOccluder::PrepareResources - Exceeded maximum geometry update limit of {}", MAX_MESHES);
						break;
					}

					numVertices = std::max(numVertices, mesh->vertexData.count);

					m_VertexUpdateData[meshIndex++] = LandLODUpdate(
						mesh->m_DescriptorHandle.Get(),
						mesh->vertexData.count,
						mesh->m_LocalToRoot,
						instance->m_Transform);
				}

				// Marks Vertex as dirty, triggering a BLAS update on SceneTLAS pass
				if (meshIndex > firstMeshIndex)
					instance->model->TerrainLODUpdated();
			}
		}

		if (meshIndex == 0)
			return false;

		commandList->writeBuffer(m_Buffer, m_VertexUpdateData.data(), sizeof(LandLODUpdate) * meshIndex);

		numMeshes = meshIndex;

		return true;
	}

	void LandLODOccluder::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(float4)),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_Buffer)
		};

		m_BindingSet = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void LandLODOccluder::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		uint32_t numMeshes = 0;
		uint32_t vertexCount = 0;

		if (!PrepareResources(commandList, numMeshes, vertexCount))
			return;

		auto* scene = Scene::GetSingleton();
		auto* sceneGraph = scene->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
			sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable
		};

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = bindings;
		commandList->setComputeState(state);

		float4 loadedRange = *reinterpret_cast<float4*>(&RE::BSShaderManager::State::GetSingleton().loadedRange);

		// The original code subtracts posAdjust.x/y but the world matrix used in the original shader does not contain translation (posAdjust)
		float4 highDetailRange = loadedRange - float4(0, 0, 15.0f, 15.0f);
		commandList->setPushConstants(&highDetailRange, sizeof(float4));

		auto vertexGroups = Util::Math::DivideRoundUp(vertexCount, 32u);
		commandList->dispatch(numMeshes, vertexGroups);
	}
}
