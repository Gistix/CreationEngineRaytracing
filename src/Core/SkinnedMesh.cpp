#include "Core/SkinnedMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Constants.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Utils/Adapter.h"
#include "Renderer/RenderNode.h"
#include "Pass/Raytracing/Common/Skinning.h"
#include "Types/RE/RE.h"

SkinnedMesh::SkinnedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;
	m_Type = Type::Skinned;

	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	auto* skinInstance = geometryData.skinInstance.get();
	if (!skinInstance) {
		logger::warn("SkinnedMesh::SkinnedMesh - No skin instance for {}", m_Name);
		return;
	}

	const auto& skinPartition = skinInstance->skinPartition;
	if (!skinPartition || skinPartition->numPartitions == 0) {
		logger::warn("SkinnedMesh::SkinnedMesh - No skin partitions for {}", m_Name);
		return;
	}

	auto* basePartitionBuffer = skinPartition->partitions[0].buffData;
	if (!basePartitionBuffer) {
		logger::warn("SkinnedMesh::SkinnedMesh - No base partition buffer for {}", m_Name);
		return;
	}

	const uint32_t vertexCount = skinPartition->vertexCount;

	if (!ValidateCounts(skinPartition->partitions[0].triangles, vertexCount))
		return;

	// All partitions share a single vertex buffer; create it once from the first partition.
	// This native buffer is the original (rest-pose) source consumed by the skinning pass.
	m_VertexBuffer = CreateVertexBuffer(basePartitionBuffer);
	if (!m_VertexBuffer.m_Buffer)
		return;

	m_VertexCount = vertexCount;

	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(basePartitionBuffer->vertexDesc);

	// Create the live (output) buffer + prev positions and register everything at the shared slot.
	// The BLAS reads the live buffer (skinning output), not the native original.
	nvrhi::IBuffer* liveBuffer = CreateSkinningBuffers(commandList, basePartitionBuffer, vertexCount, vertexStride);

	BuildSkinned(bsTriShape, liveBuffer, vertexStride, true);
}

nvrhi::IBuffer* SkinnedMesh::CreateSkinningBuffers(nvrhi::ICommandList* commandList, RE::BSGraphics::TriShape* sourceTriShape, uint32_t vertexCount, uint16_t vertexStride)
{
	auto device = Renderer::GetSingleton()->GetDevice();
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	const uint32_t slot = m_VertexBuffer.m_Descriptor.Get();
	const size_t vertexBufferSize = m_VertexBuffer.m_Buffer->getDesc().byteSize;

	// Live (output) buffer: device-owned, raw-viewable + UAV + BLAS input.
	auto liveBufferDesc = nvrhi::BufferDesc()
		.setByteSize(vertexBufferSize)
		.setCanHaveRawViews(true)
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} (Live Vertex Buffer)", m_Name.c_str()).c_str());

	m_LiveVertexBuffer = device->createBuffer(liveBufferDesc);

	// Seed the live buffer from the CPU rest-pose data (carries UV/color/etc. that skinning never writes).
	// Avoids barriering the shared native source buffer, which several meshes wrap independently.
	const size_t seedSize = static_cast<size_t>(vertexCount) * vertexStride;
	commandList->writeBuffer(m_LiveVertexBuffer, Util::Adapter::GetVertexData(sourceTriShape), seedSize);

	// Prev-position buffer (float3 per vertex) for per-vertex motion vectors.
	auto prevPositionBufferDesc = nvrhi::BufferDesc()
		.setByteSize(sizeof(float3) * vertexCount)
		.setStructStride(sizeof(float3))
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setDebugName(std::format("{} (Prev Position Buffer)", m_Name.c_str()).c_str());

	m_PrevPositionBuffer = device->createBuffer(prevPositionBufferDesc);

	// RT reads the live buffer (repoint the slot previously set to the native buffer).
	device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
		nvrhi::BindingSetItem::RawBuffer_SRV(slot, m_LiveVertexBuffer));

	// Skinning reads the original (native) buffer.
	device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_SRV(slot, m_VertexBuffer.m_Buffer));

	// Skinning writes the live buffer.
	device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_UAV(slot, m_LiveVertexBuffer));

	// Prev positions: SRV (RT read) + UAV (skinning write).
	device->writeDescriptorTable(sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_SRV(slot, m_PrevPositionBuffer));
	device->writeDescriptorTable(sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_UAV(slot, m_PrevPositionBuffer));

	return m_LiveVertexBuffer;
}

void SkinnedMesh::BuildSkinned(RE::BSTriShape* bsTriShape, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, bool requireSharedNativeVertexBuffer)
{
	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	auto* skinInstance = geometryData.skinInstance.get();
	if (!skinInstance)
		return;

	const auto& skinPartition = skinInstance->skinPartition;
	if (!skinPartition || skinPartition->numPartitions == 0)
		return;

	auto* basePartitionBuffer = skinPartition->partitions[0].buffData;

	const uint32_t vertexCount = skinPartition->vertexCount;

	std::memcpy(&m_VertexDescRaw, &basePartitionBuffer->vertexDesc, sizeof(m_VertexDescRaw));

	m_IndexBuffers.reserve(skinPartition->numPartitions);
	m_GeometryDescs.reserve(skinPartition->numPartitions);

	for (size_t i = 0; i < skinPartition->numPartitions; i++)
	{
		const auto& partition = skinPartition->partitions[i];

		auto* partitionBuffer = partition.buffData;
		if (!partitionBuffer) {
			logger::warn("SkinnedMesh::BuildSkinned - Partition {} has no buffer for {}, skipping partition.", i, m_Name);
			continue;
		}

		if (partition.triangles == 0)
			continue;

		// Enforce the single-vertex-buffer invariant: every partition must reference the same vertex buffer.
		if (requireSharedNativeVertexBuffer && partitionBuffer->vertexBuffer != basePartitionBuffer->vertexBuffer) {
			logger::warn("SkinnedMesh::BuildSkinned - Partition {} vertex buffer differs from partition 0 for {}, skipping mesh.", i, m_Name);
			m_IndexBuffers.clear();
			m_GeometryDescs.clear();
			m_VertexBuffer = {};
			return;
		}

		auto indexBuffer = CreateIndexBuffer(partitionBuffer);
		if (!indexBuffer.m_Buffer)
			continue;

		const uint32_t indexCount = static_cast<uint32_t>(partition.triangles) * 3;

		auto& emplacedIndexBuffer = m_IndexBuffers.emplace_back(std::move(indexBuffer));
		m_GeometryDescs.push_back(MakeGeometryDesc(emplacedIndexBuffer.m_Buffer, indexCount, vertexBuffer, vertexStride, vertexCount));
	}
}

bool SkinnedMesh::Update()
{
	bool poseAdvanced = false;

#if defined(SKYRIM)
	if (m_BSTriShape) {
		const auto& geometryData = m_BSTriShape->GetGeometryRuntimeData();

		auto* skinInstance = geometryData.skinInstance.get();

		// UBE crash fix: rootParent must be valid.
		if (skinInstance && skinInstance->rootParent) {
			// Only recompute when the game advanced the animation this frame.
			const auto frameID = skinInstance->frameID;
			if (m_SkinFrameID != frameID) {
				m_SkinFrameID = frameID;

				auto* skinData = skinInstance->skinData.get();
				if (skinData && skinData->bones != 0) {
					const auto geometryWorldInverse = m_BSTriShape->world.Invert();

					if (m_BoneMatrices.size() != skinData->bones)
						m_BoneMatrices.resize(skinData->bones);

					for (uint32_t i = 0; i < skinData->bones; i++) {
						const auto boneWorld = *skinInstance->boneWorldTransforms[i];
						const auto boneMatrix = boneWorld * skinData->boneData[i].skinToBone;
						XMStoreFloat3x4(&m_BoneMatrices[i], Util::Math::GetXMFromNiTransform(geometryWorldInverse * boneMatrix));
					}

					poseAdvanced = true;
				}
			}
		}
	}
#endif

	// Queue this mesh for the GPU skinning pass when the pose advanced or its vertices changed.
	DirtyFlags skinningFlags = DirtyFlags::None;
	if (poseAdvanced)
		skinningFlags |= DirtyFlags::Skin;
	if (GetDirtyFlags().any(DirtyFlags::Vertex))
		skinningFlags |= DirtyFlags::Vertex;

	if (skinningFlags != DirtyFlags::None) {
		if (auto* skinningPass = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>())
			skinningPass->QueueUpdate(skinningFlags, this);
	}

	return poseAdvanced;
}
