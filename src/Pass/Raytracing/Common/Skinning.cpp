#include "Skinning.h"
#include "Renderer.h"
#include "Scene.h"

namespace Pass
{
	Skinning::Skinning(Renderer* renderer)
		: RenderPass(renderer)
	{
		auto device = renderer->GetDevice();
		m_VertexUpdateBuffer = Util::CreateStructuredBuffer<VertexUpdateData>(device, MAX_GEOMETRY, "Vertex Update Buffer");
		m_BoneMatrixBuffer = Util::CreateStructuredBuffer<BoneMatrix>(device, MAX_BONE_MATRICES, "Bone Matrix Buffer");

		CreateBindingLayout();
		CreatePipeline();
	}

	void Skinning::CreateBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1)
		};

		m_BindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
	}

	void Skinning::CreatePipeline()
	{
		auto device = GetRenderer()->GetDevice();

		winrt::com_ptr<IDxcBlob> shaderBlob;
		ShaderUtils::CompileShader(shaderBlob, L"data/shaders/Skinning.hlsl", {}, L"cs_6_5");
		m_ComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

		if (!m_ComputeShader)
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		auto pipelineDesc = nvrhi::ComputePipelineDesc()
			.setComputeShader(m_ComputeShader)
			.addBindingLayout(m_BindingLayout)
			.addBindingLayout(sceneGraph->GetDynamicVertexDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexCopyDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetSkinningDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexWriteDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetPrevPositionWriteDescriptors()->m_Layout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void Skinning::QueueUpdate(DirtyFlags updateFlags, Mesh* mesh)
	{
		queuedMeshes.emplace(
			mesh,
			QueuedMesh(updateFlags, mesh->m_Name));
	}

	bool Skinning::PrepareResources(nvrhi::ICommandList* commandList, uint32_t& numMeshes, uint32_t& numVertices)
	{
		if (queuedMeshes.empty())
			return false;

		uint32_t shapeIndex = 0;
		uint32_t boneMatrixIndex = 0;

		for (auto& [mesh, queuedMesh] : queuedMeshes) {
			if (shapeIndex >= MAX_GEOMETRY - 1) {
				logger::critical("SkinningPipeline::PrepareResources - Exceeded maximum geometry update limit of {}", MAX_GEOMETRY);
				break;
			}

			const bool vertexUpdate = (queuedMesh.updateFlags & DirtyFlags::Vertex) != DirtyFlags::None;
			const bool skinUpdate = (queuedMesh.updateFlags & DirtyFlags::Skin) != DirtyFlags::None;

			numVertices = std::max(numVertices, mesh->vertexData.count);
			uint32_t numBoneMatrices = skinUpdate ? static_cast<uint32_t>(mesh->m_BoneMatrices.size()) : 0;

			auto& vertexUpdateData = m_VertexUpdateData[shapeIndex];

			vertexUpdateData = VertexUpdateData(
				mesh->m_DescriptorHandle.Get(),
				static_cast<uint32_t>(queuedMesh.updateFlags),
				mesh->vertexData.count,
				boneMatrixIndex,
				mesh->flags.underlying(),
				numBoneMatrices);

			shapeIndex++;

			// Dynamic TriShapes
			if (vertexUpdate)
				mesh->UpdateUploadDynamicBuffers(commandList);

			// Skinning - This is a bit more involved
			if (skinUpdate) {
				if (numBoneMatrices > MAX_BONE_MATRICES - boneMatrixIndex) {
					logger::critical(
						"SkinningPipeline::PrepareResources - Bone matrix upload for '{}' would exceed the maximum of {} (offset {}, count {})",
						queuedMesh.path,
						MAX_BONE_MATRICES,
						boneMatrixIndex,
						numBoneMatrices);
					break;
				}

				std::memcpy(m_BoneMatrixData.data() + boneMatrixIndex, mesh->m_BoneMatrices.data(), sizeof(float3x4) * numBoneMatrices);
				boneMatrixIndex += numBoneMatrices;
			}
		}

		commandList->writeBuffer(m_VertexUpdateBuffer, m_VertexUpdateData.data(), sizeof(VertexUpdateData) * shapeIndex);
		if (boneMatrixIndex > 0)
			commandList->writeBuffer(m_BoneMatrixBuffer, m_BoneMatrixData.data(), sizeof(BoneMatrix) * boneMatrixIndex);

		numMeshes = shapeIndex;

		return true;
	}

	void Skinning::ClearQueue()
	{
		queuedMeshes.clear();
	}

	void Skinning::CheckBindings()
	{
		if (!m_DirtyBindings)
			return;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_VertexUpdateBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_BoneMatrixBuffer)
		};

		m_BindingSet = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_DirtyBindings = false;
	}

	void Skinning::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		uint32_t numMeshes = 0;
		uint32_t vertexCount = 0;

		if (!PrepareResources(commandList, numMeshes, vertexCount))
			return;

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSet,
			sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable,
			sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
			sceneGraph->GetSkinningDescriptors()->m_DescriptorTable,
			sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable
		};

		nvrhi::ComputeState state;
		state.pipeline = m_ComputePipeline;
		state.bindings = bindings;
		commandList->setComputeState(state);

		auto vertexGroups = Util::Math::DivideRoundUp(vertexCount, 32u);
		commandList->dispatch(numMeshes, vertexGroups);

		ClearQueue();
	}
}
