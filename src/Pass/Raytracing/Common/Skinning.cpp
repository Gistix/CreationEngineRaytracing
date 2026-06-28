#include "Skinning.h"
#include "Renderer.h"
#include "Scene.h"
#include "Core/SkinnedMesh.h"
#include "Core/DynamicMesh.h"

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
			.addBindingLayout(sceneGraph->GetDynamicVertexReadDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexCopyDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetVertexWriteDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetPrevPositionWriteDescriptors()->m_Layout)
			.addBindingLayout(sceneGraph->GetDynamicVertexWriteDescriptors()->m_Layout);

		m_ComputePipeline = device->createComputePipeline(pipelineDesc);
	}

	void Skinning::QueueUpdate(DirtyFlags updateFlags, SkinnedMesh* mesh)
	{
		queuedMeshes.emplace(
			mesh,
			QueuedMesh(updateFlags, mesh->GetName()));
	}

	bool Skinning::PrepareResources(nvrhi::ICommandList* commandList, uint32_t& numMeshes, uint32_t& numVertices)
	{
		if (queuedMeshes.empty())
			return false;

		uint32_t meshIndex = 0;
		uint32_t boneMatrixIndex = 0;

		for (auto& [mesh, queuedMesh] : queuedMeshes) {
			if (meshIndex > MAX_GEOMETRY - 1) {
				logger::critical("SkinningPipeline::PrepareResources - Exceeded maximum geometry update limit of {}", MAX_GEOMETRY);
				break;
			}

			const bool vertexUpdate = (queuedMesh.updateFlags & DirtyFlags::Vertex) != DirtyFlags::None;
			const bool skinUpdate = (queuedMesh.updateFlags & DirtyFlags::Skin) != DirtyFlags::None;

			const uint32_t vertexCount = mesh->GetVertexCount();
			numVertices = std::max(numVertices, vertexCount);

			const auto& boneMatrices = mesh->GetBoneMatrices();
			const uint32_t numBoneMatrices = skinUpdate ? static_cast<uint32_t>(boneMatrices.size()) : 0;

			if (skinUpdate && numBoneMatrices > MAX_BONE_MATRICES - boneMatrixIndex) {
				logger::critical(
					"SkinningPipeline::PrepareResources - Bone matrix upload for '{}' would exceed the maximum of {} (offset {}, count {})",
					queuedMesh.path,
					MAX_BONE_MATRICES,
					boneMatrixIndex,
					numBoneMatrices);
				break;
			}

			auto* dynamicMesh = mesh->AsDynamicMesh();
			const uint32_t dynamicIndex = dynamicMesh ? dynamicMesh->GetDynamicIndex() : 0u;
			uint32_t meshFlags = dynamicMesh ? ::Skinning::MeshFlags::Dynamic : 0u;

			if (mesh->GetModelSpaceNormal())
				meshFlags |= ::Skinning::MeshFlags::ModelSpaceNormal;

			const uint64_t vertexDescRaw = mesh->GetVertexDescRaw();

			VertexUpdateData& data = m_VertexUpdateData[meshIndex++];
			data.index = mesh->GetSkinningSlot();
			data.dynamicIndex = dynamicIndex;
			data.updateFlags = static_cast<uint32_t>(queuedMesh.updateFlags);
			data.vertexCount = vertexCount;
			data.boneOffset = boneMatrixIndex;
			data.meshFlags = meshFlags;
			data.numMatrices = numBoneMatrices;
			std::memcpy(&data.VertexDesc, &vertexDescRaw, sizeof(uint64_t));

			// Dynamic morph upload (skinning input) must land before the dispatch reads it.
			if (vertexUpdate && dynamicMesh)
				dynamicMesh->UploadBuffers(commandList);

			if (skinUpdate) {
				std::memcpy(m_BoneMatrixData.data() + boneMatrixIndex, boneMatrices.data(), sizeof(float3x4) * numBoneMatrices);
				boneMatrixIndex += numBoneMatrices;
			}
		}

		commandList->writeBuffer(m_VertexUpdateBuffer, m_VertexUpdateData.data(), sizeof(VertexUpdateData) * meshIndex);
		if (boneMatrixIndex > 0)
			commandList->writeBuffer(m_BoneMatrixBuffer, m_BoneMatrixData.data(), sizeof(BoneMatrix) * boneMatrixIndex);

		numMeshes = meshIndex;

		return true;
	}

	void Skinning::ClearQueue()
	{
		queuedMeshes.clear();
	}

	void Skinning::CheckBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BindingSetDirty[currentSlot] && m_BindingSets[currentSlot])
			return;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_VertexUpdateBuffer),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_BoneMatrixBuffer)
		};

		m_BindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BindingLayout);

		m_BindingSetDirty[currentSlot] = false;
	}

	void Skinning::Execute(nvrhi::ICommandList* commandList)
	{
		CheckBindings();

		uint32_t numMeshes = 0;
		uint32_t vertexCount = 0;

		if (!PrepareResources(commandList, numMeshes, vertexCount))
			return;

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		nvrhi::BindingSetVector bindings = {
			m_BindingSets[currentSlot],
			sceneGraph->GetDynamicVertexReadDescriptors()->m_DescriptorTable->GetDescriptorTable(),
			sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
			sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable,
			sceneGraph->GetDynamicVertexWriteDescriptors()->m_DescriptorTable
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
