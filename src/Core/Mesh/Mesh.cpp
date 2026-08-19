#include "Core/Mesh/Mesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"
#include "Types/RE/RE.h"

Mesh::Mesh(RE::BSTriShape* bsTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;
	m_Type = Type::Default;

	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);

	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("Mesh::Mesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(bsTriShape);

	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount))
		return;

	m_VertexDesc = rendererData->vertexDesc;

	m_IndexBuffer = CreateIndexBuffer(rendererData);
	if (!m_IndexBuffer.m_Buffer)
		return;

	m_VertexBuffer = CreateVertexBuffer(rendererData);
	if (!m_VertexBuffer.m_Buffer)
		return;

	AllocateMeshIndex();

	const uint32_t indexCount = static_cast<uint32_t>(triShapeData.triangleCount) * 3;
	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(rendererData->vertexDesc);

	m_GeometryEntries.push_back({ MakeGeometryDesc(m_IndexBuffer.m_Buffer, m_IndexBuffer.m_Offset, indexCount, m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, vertexStride, triShapeData.vertexCount, GetMeshIndex()), AllocateGeometryIndex() });

	CreateMaterial();
}
