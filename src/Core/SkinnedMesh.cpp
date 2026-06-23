#include "Core/SkinnedMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

SkinnedMesh::SkinnedMesh(RE::BSTriShape* bsTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;

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
	m_VertexBuffer = CreateVertexBuffer(basePartitionBuffer);
	if (!m_VertexBuffer.m_Buffer)
		return;

	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(basePartitionBuffer->vertexDesc);

	BuildSkinned(bsTriShape, m_VertexBuffer.m_Buffer, vertexStride, true);
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
