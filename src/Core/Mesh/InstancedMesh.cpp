#include "Core/Mesh/InstancedMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"
#include "Types.h"

InstancedMesh::InstancedMesh(RE::BSTriShape* bsTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;
	m_Type = Type::Instanced;

	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);
	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("InstancedMesh::InstancedMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(bsTriShape);
	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount))
		return;

	m_VertexDesc = rendererData->vertexDesc;
	m_IndexBuffer = CreateIndexBuffer(rendererData);
	m_VertexBuffer = CreateVertexBuffer(rendererData);

	AllocateMeshIndex();

	const uint32_t indexCount = static_cast<uint32_t>(triShapeData.triangleCount) * 3;
	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(rendererData->vertexDesc);
	const nvrhi::Format vertexFormat = Util::Geometry::GetVertexPositionFormat(rendererData->vertexDesc);

	m_GeometryEntries.push_back({ MakeGeometryDesc(
		m_IndexBuffer.m_Buffer, m_IndexBuffer.m_Offset, indexCount,
		m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, vertexStride, triShapeData.vertexCount,
		GetMeshIndex(), vertexFormat), AllocateGeometryIndex() });

	CreateMaterial();

	if (bsTriShape->GetType() == RE::BSGeometry::Type::kMultiStreamInstanceTriShape)
	{
		auto instanceData = Scene::GetSingleton()->GetSceneGraph()->GetBlockInstanceData(bsTriShape);

		if (instanceData.empty()) {
			logger::warn("Failed to find instance data for {} {}", magic_enum::enum_name(bsTriShape->GetType().get()), GetName().c_str());
			return;
		}

		for (auto& data : instanceData)
		{
			half3 position;
			std::memcpy(&position, &data.x, sizeof(half3));

			half rotZ;
			std::memcpy(&rotZ, &data.rotZ, sizeof(half));

			half scale;
			std::memcpy(&scale, &data.scale, sizeof(half));

			auto instanceTransform = RE::NiTransform();
			instanceTransform.rotate = RE::NiMatrix3(0.0f, 0.0f, rotZ * (180.0f / std::numbers::pi_v<float>));
			instanceTransform.translate = RE::NiPoint3(position.x, position.y, position.z);
			instanceTransform.scale = scale;

			m_InstanceData.emplace_back(instanceTransform, 1.0f);
		}
	}
}