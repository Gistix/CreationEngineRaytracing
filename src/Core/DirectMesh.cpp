#include "Core/DirectMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Types/RE/RE.h"

DirectMesh::DirectMesh(RE::BSTriShape* bsTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;
	m_Type = Type::Default;

	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("DirectMesh::DirectMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = bsTriShape->GetTrishapeRuntimeData();

	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount))
		return;

	m_VertexDesc = rendererData->vertexDesc;

	m_IndexBuffer = CreateIndexBuffer(rendererData);
	if (!m_IndexBuffer.m_Buffer)
		return;

	m_VertexBuffer = CreateVertexBuffer(rendererData);
	if (!m_VertexBuffer.m_Buffer)
		return;

	const uint32_t indexCount = static_cast<uint32_t>(triShapeData.triangleCount) * 3;
	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(rendererData->vertexDesc);

	m_GeometryDescs.push_back(MakeGeometryDesc(m_IndexBuffer.m_Buffer, 0, indexCount, m_VertexBuffer.m_Buffer, vertexStride, triShapeData.vertexCount));

	CreateMaterial();
}
