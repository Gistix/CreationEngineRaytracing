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

		for (uint32_t i = 0; i < Constants::MAX_FRAMES_IN_FLIGHT; i++) {
			m_BoneWorldBuffer[i] = Util::CreateStructuredBuffer<NiTransformPacked>(device, MAX_BONE_MATRICES,
				std::format("Bone World Buffer[{}]", i).c_str());
			m_SkinToBoneBuffer[i] = Util::CreateStructuredBuffer<NiTransformPacked>(device, MAX_BONE_MATRICES,
				std::format("SkinToBone Buffer[{}]", i).c_str());
			m_MeshBoneHeaderBuffer[i] = Util::CreateStructuredBuffer<MeshBoneHeader>(device, MAX_GEOMETRY,
				std::format("Mesh Bone Header Buffer[{}]", i).c_str());
		}

		m_VertexUpdateBuffer = Util::CreateStructuredBuffer<VertexUpdateData>(device, MAX_GEOMETRY, "Vertex Update Buffer");
		m_BoneMatrixBuffer = Util::CreateStructuredBuffer<BoneMatrix>(device, MAX_BONE_MATRICES, "Bone Matrix Buffer", true);

		CreateBoneBindingLayout();
		CreateBindingLayout();
		CreatePipeline();
	}

	void Skinning::CreateBoneBindingLayout()
	{
		nvrhi::BindingLayoutDesc globalBindingLayoutDesc;
		globalBindingLayoutDesc.visibility = nvrhi::ShaderType::Compute;
		globalBindingLayoutDesc.bindings = {
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1),
			nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2),
			nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)
		};

		m_BoneBindingLayout = GetRenderer()->GetDevice()->createBindingLayout(globalBindingLayoutDesc);
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

		// Bone compute pipeline
		{
			winrt::com_ptr<IDxcBlob> shaderBlob;
			ShaderUtils::CompileShader(shaderBlob, L"data/shaders/BoneCompute.hlsl", {}, L"cs_6_5");
			if (shaderBlob) {
				m_BoneComputeShader = device->createShader({ nvrhi::ShaderType::Compute, "", "Main" }, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

				if (m_BoneComputeShader) {
					auto pipelineDesc = nvrhi::ComputePipelineDesc()
						.setComputeShader(m_BoneComputeShader)
						.addBindingLayout(m_BoneBindingLayout);

					m_BoneComputePipeline = device->createComputePipeline(pipelineDesc);
				}
			}
		}

		// Vertex skinning pipeline
		{
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

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		uint32_t meshIndex = 0;
		uint32_t boneIndex = 0;

		for (auto& [mesh, queuedMesh] : queuedMeshes) {
			if (meshIndex > MAX_GEOMETRY - 1) {
				logger::critical("Skinning::PrepareResources - Exceeded maximum geometry update limit of {}", MAX_GEOMETRY);
				break;
			}

			const bool vertexUpdate = (queuedMesh.updateFlags & DirtyFlags::Vertex) != DirtyFlags::None;
			const bool skinUpdate = (queuedMesh.updateFlags & DirtyFlags::Skin) != DirtyFlags::None;

			const uint32_t vertexCount = mesh->GetVertexCount();
			numVertices = std::max(numVertices, vertexCount);

			const uint32_t boneCount = skinUpdate ? mesh->GetBoneCount() : 0;

			if (skinUpdate && boneCount > MAX_BONE_MATRICES - boneIndex) {
				logger::critical(
					"Skinning::PrepareResources - Bone data upload for '{}' would exceed the maximum of {} (offset {}, count {})",
					queuedMesh.path,
					MAX_BONE_MATRICES,
					boneIndex,
					boneCount);
				break;
			}

			auto* dynamicMesh = mesh->AsDynamicMesh();
			const uint32_t dynamicIndex = dynamicMesh ? dynamicMesh->GetDynamicIndex() : 0u;
			uint32_t meshFlags = dynamicMesh ? ::Skinning::MeshFlags::Dynamic : 0u;

			if (mesh->GetModelSpaceNormal())
				meshFlags |= ::Skinning::MeshFlags::ModelSpaceNormal;

			const uint64_t vertexDescRaw = mesh->GetVertexDescRaw();

			// Populate vertex update data (consumed by the skinning pass)
			VertexUpdateData& data = m_VertexUpdateData[meshIndex];
			data.index = mesh->GetSkinningSlot();
			data.dynamicIndex = dynamicIndex;
			data.updateFlags = static_cast<uint32_t>(queuedMesh.updateFlags);
			data.vertexCount = vertexCount;
			data.boneOffset = boneIndex;
			data.meshFlags = meshFlags;
			data.numMatrices = boneCount;
			std::memcpy(&data.VertexDesc, &vertexDescRaw, sizeof(uint64_t));

			// Dynamic morph upload (skinning input) must land before the dispatch reads it.
			if (vertexUpdate && dynamicMesh)
				dynamicMesh->UploadBuffers(commandList);

			if (skinUpdate && boneCount > 0) {
				// Copy raw boneWorld transforms to BoneCompute input buffer
				std::memcpy(m_BoneWorldData.data() + boneIndex,
					mesh->GetBoneWorlds().data(),
					sizeof(NiTransformPacked) * boneCount);

				// Copy static skinToBone transforms to BoneCompute input buffer
				std::memcpy(m_SkinToBoneData.data() + boneIndex,
					mesh->GetSkinToBones().data(),
					sizeof(NiTransformPacked) * boneCount);

				// Fill per-mesh bone compute header
				MeshBoneHeader& header = m_MeshBoneHeaderData[meshIndex];
				header.BoneCount = boneCount;
				header.BoneWorldOffset = boneIndex;
				header.SkinToBoneOffset = boneIndex;
				header.Pad = 0;
				NiTransformPacked geomInv = mesh->GetGeometryWorldInverse();
				header.GeomInv_Rot0_Scale = geomInv.Rot0_Scale;
				header.GeomInv_Rot1       = geomInv.Rot1;
				header.GeomInv_Rot2       = geomInv.Rot2;
				header.GeomInv_Translate  = geomInv.Translate;
			}

			boneIndex += boneCount;
			meshIndex++;
		}

		if (meshIndex == 0)
			return false;

		// Upload raw bone transform data (BoneCompute input)
		if (boneIndex > 0) {
			auto bytes = sizeof(NiTransformPacked) * boneIndex;

			if (bytes > 0) {
				commandList->writeBuffer(m_BoneWorldBuffer[currentSlot], m_BoneWorldData.data(), bytes);
				commandList->writeBuffer(m_SkinToBoneBuffer[currentSlot], m_SkinToBoneData.data(), bytes);
			}
		}

		commandList->writeBuffer(m_MeshBoneHeaderBuffer[currentSlot], m_MeshBoneHeaderData.data(), sizeof(MeshBoneHeader) * meshIndex);

		// Upload vertex skinning data (Skinning pass input)
		commandList->writeBuffer(m_VertexUpdateBuffer, m_VertexUpdateData.data(), sizeof(VertexUpdateData) * meshIndex);

		numMeshes = meshIndex;

		return true;
	}

	void Skinning::ClearQueue()
	{
		queuedMeshes.clear();
	}

	void Skinning::CheckBoneBindings()
	{
		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		if (!m_BoneBindingSetDirty[currentSlot] && m_BoneBindingSets[currentSlot])
			return;

		nvrhi::BindingSetDesc bindingSetDesc;
		bindingSetDesc.bindings = {
			nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_BoneWorldBuffer[currentSlot]),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_SkinToBoneBuffer[currentSlot]),
			nvrhi::BindingSetItem::StructuredBuffer_SRV(2, m_MeshBoneHeaderBuffer[currentSlot]),
			nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_BoneMatrixBuffer)
		};

		m_BoneBindingSets[currentSlot] = GetRenderer()->GetDevice()->createBindingSet(bindingSetDesc, m_BoneBindingLayout);

		m_BoneBindingSetDirty[currentSlot] = false;
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
		CheckBoneBindings();
		CheckBindings();

		uint32_t numMeshes = 0;
		uint32_t vertexCount = 0;

		if (!PrepareResources(commandList, numMeshes, vertexCount))
			return;

		uint32_t currentSlot = GetRenderer()->GetCurrentSlot();
		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

		// --- Pass 0: Bone Matrix Compute ---
		// Compute M = geometryWorldInverse * boneWorld * skinToBone per bone, store in BoneMatrixBuffer.
		if (m_BoneComputePipeline) {
			nvrhi::ComputeState boneState;
			boneState.pipeline = m_BoneComputePipeline;
			boneState.bindings = { m_BoneBindingSets[currentSlot] };
			commandList->setComputeState(boneState);

			uint32_t totalBones = 0;
			for (auto& [mesh, qm] : queuedMeshes)
				if ((qm.updateFlags & DirtyFlags::Skin) != DirtyFlags::None)
					totalBones += mesh->GetBoneCount();

			if (totalBones > 0) {
				auto boneGroups = Util::Math::DivideRoundUp(totalBones, 64u);
				commandList->dispatch(boneGroups, numMeshes);
				commandList->commitBarriers();
			}
		}

		// --- Pass 1: Vertex Skinning ---
		{
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
		}

		ClearQueue();
	}
}
