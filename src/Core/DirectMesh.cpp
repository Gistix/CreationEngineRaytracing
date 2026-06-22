#include "Core/DirectMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DirectMesh::DirectMesh(RE::BSTriShape* bsTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;

	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("DirectMesh::DirectMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = bsTriShape->GetTrishapeRuntimeData();

	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount, rendererData))
		return;

	m_IndexBuffer = CreateIndexBuffer(rendererData);
	if (!m_IndexBuffer)
		return;

	m_VertexBuffer = CreateVertexBuffer(rendererData);
	if (!m_VertexBuffer)
		return;

	const uint32_t indexCount = static_cast<uint32_t>(triShapeData.triangleCount) * 3;
	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(rendererData->vertexDesc);

	m_GeometryDescs.push_back(MakeGeometryDesc(m_IndexBuffer, indexCount, m_VertexBuffer, vertexStride, triShapeData.vertexCount));
}
