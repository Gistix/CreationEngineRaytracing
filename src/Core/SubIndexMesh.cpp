#include "Core/SubIndexMesh.h"
#include "Types/RE/RE.h"
#include "Util.h"

SubIndexMesh::SubIndexMesh(RE::BSSubIndexTriShape* subIndexTriShape, [[ maybe_unused ]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(subIndexTriShape);
	m_BSTriShape = subIndexTriShape;
	m_Type = Type::SubIndex;

	const auto& geometryData = subIndexTriShape->GetGeometryRuntimeData();

	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("SubIndexMesh::SubIndexMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = subIndexTriShape->GetTrishapeRuntimeData();

	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount)) {
		logger::error("SubIndexMesh::SubIndexMesh - Failed to validate Triangle Count: {}, Vertex Count: {}", triShapeData.triangleCount, triShapeData.vertexCount);
		return;
	}

	m_VertexDesc = rendererData->vertexDesc;

	m_IndexBuffer = CreateIndexBuffer(rendererData);

	m_VertexBuffer = CreateVertexBuffer(rendererData);

	auto triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(m_BSTriShape->GetGeometryRuntimeData().rendererData);
	m_NativeIndexBuffer = triShapeDX12->indexBufferDX12;
	m_NativeVertexBuffer = triShapeDX12->vertexBufferDX12;

	m_VertexStride = Util::Geometry::GetStoredVertexSize(rendererData->vertexDesc);

	CreateMaterial();
}

void SubIndexMesh::Update(nvrhi::ICommandList* commandList)
{
	BaseMesh::Update(commandList);

	auto* subIndexShape = Util::Adapter::AsSubIndexTriShape(m_BSTriShape);
	if (!subIndexShape)
		return;

	const auto& triShapeData = m_BSTriShape->GetTrishapeRuntimeData();

	const auto numIndices = triShapeData.triangleCount * 3u;	

	m_GeometryDescs.clear();

	auto& runtimeData = subIndexShape->GetSubIndexedTrishapeRuntimeData();
	for (size_t i = 0; i < runtimeData.numSegments; i++)
	{
		const auto& segment = runtimeData.segmentData[i];

		// Visibility
		if (segment.flags == 0u)
			continue;

		if (segment.numTris == 0)
			continue;

		const auto finalIndex = segment.index + (segment.numTris * 3u);
		if (finalIndex > numIndices) {
			logger::warn("Index {} exceeds the maximum of {}", finalIndex, numIndices);
			logger::warn("Index: {}, UnkTriCount: {}, UnkFlags: {}, NumTris: {}, Flags: {}", segment.index, segment.unkTriCount, segment.unkFlags, segment.numTris, segment.flags);
			continue;
		}
		//logger::info("Index: {}, UnkTriCount: {}, UnkFlags: {}, NumTris: {}, Flags: {}", segment.index, segment.unkTriCount, segment.unkFlags, segment.numTris, segment.flags);

		m_GeometryDescs.push_back(MakeGeometryDesc(m_IndexBuffer.m_Buffer, segment.index, segment.numTris * 3u, m_VertexBuffer.m_Buffer, m_VertexStride, triShapeData.vertexCount));
	}

	auto currSize = m_GeometryDescs.size();
	auto prevSize = m_PrevGeometryDescs.size();

	if (currSize != prevSize) {
		MarkDirty(DirtyFlags::Visibility);
		m_PrevGeometryDescs = m_GeometryDescs;
		return;
	}

	auto size = std::max(currSize, prevSize) * sizeof(nvrhi::rt::GeometryDesc);
	if (memcmp(m_GeometryDescs.data(), m_PrevGeometryDescs.data(), size) != 0) {
		MarkDirty(DirtyFlags::Visibility);
		m_PrevGeometryDescs = m_GeometryDescs;
	}
}
