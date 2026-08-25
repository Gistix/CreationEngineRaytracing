#include "Core/Mesh/MergeInstancedMesh.h"

#if defined(FALLOUT4)
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"

#include <DirectXMath.h>

using namespace DirectX;

MergeInstancedMesh::MergeInstancedMesh(RE::BSTriShape* bsTriShape, [[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;
	m_Type = Type::MergeInstanced;

	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsTriShape);
	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("MergeInstancedMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(bsTriShape);
	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount))
		return;

	m_VertexDesc = rendererData->vertexDesc;
	m_IndexBuffer = CreateIndexBuffer(rendererData);
	m_VertexBuffer = CreateVertexBuffer(rendererData);

	AllocateMeshIndex();
	CreateMaterial();
}

MergeInstancedMesh::~MergeInstancedMesh()
{
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	if (sceneGraph) {
		auto& meshManager = sceneGraph->GetMeshManager();
		for (auto meshIndex : m_ChunkMeshIndices) {
			if (meshIndex != UINT32_MAX && meshIndex != UINT16_MAX) {
				meshManager->ReleaseMeshIndex(static_cast<uint16_t>(meshIndex));
			}
		}
	}
	m_ChunkMeshIndices.clear();
}

void MergeInstancedMesh::OnDestroy()
{
	Scene::GetSingleton()->GetSceneGraph()->RemoveMergeShapeData(m_BSTriShape);
	BaseMesh::OnDestroy();
}

uint32_t MergeInstancedMesh::GetShapeDataCount() const
{
	const auto* data = Scene::GetSingleton()->GetSceneGraph()->GetMergeShapeData(m_BSTriShape);
	return data ? static_cast<uint32_t>(data->size()) : 0;
}

const RE::BSMergeInstancedTriShape::ShapeData* MergeInstancedMesh::GetShapeData() const
{
	const auto* data = Scene::GetSingleton()->GetSceneGraph()->GetMergeShapeData(m_BSTriShape);
	return (data && !data->empty()) ? data->data() : nullptr;
}

const uint16_t* MergeInstancedMesh::GetLookupData() const
{
	const auto* data = Scene::GetSingleton()->GetSceneGraph()->GetMergeLookupData(m_BSTriShape);
	return (data && !data->empty()) ? data->data() : nullptr;
}

uint32_t MergeInstancedMesh::GetLookupCount() const
{
	const auto* data = Scene::GetSingleton()->GetSceneGraph()->GetMergeLookupData(m_BSTriShape);
	return data ? static_cast<uint32_t>(data->size()) : 0;
}

uint16_t MergeInstancedMesh::GetIndexID([[maybe_unused]] size_t geometryIndex) const
{
	return static_cast<uint16_t>(m_IndexBuffer.m_Descriptor.Get());
}

uint16_t MergeInstancedMesh::GetVertexID() const
{
	return static_cast<uint16_t>(m_VertexBuffer.m_Descriptor.Get());
}

void MergeInstancedMesh::Update([[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Properties.Update(m_BSTriShape, m_Flags.all(Flags::Eyes));
	m_WorldBound = m_BSTriShape->worldBound;

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	auto& meshManager = sceneGraph->GetMeshManager();

	const auto* shapeData = GetShapeData();
	const uint32_t shapeCount = GetShapeDataCount();
	const auto* lookupData = GetLookupData();
	const uint32_t lookupCount = GetLookupCount();

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(m_BSTriShape);

	uint32_t numChunks = 0;
	if (lookupData && lookupCount > 0) {
		numChunks = lookupCount;
	} else if (shapeData && shapeCount > 0) {
		numChunks = shapeCount;
	}

	if (numChunks > 0 && triShapeData.triangleCount > 0) {
		const uint32_t trianglesPerChunk = triShapeData.triangleCount / numChunks;

		if (m_GeometryEntries.size() != numChunks && trianglesPerChunk > 0) {
			for (const auto& entry : m_GeometryEntries)
				meshManager->ReleaseGeometryIndex(entry.geometryIndex);
			for (auto meshIndex : m_ChunkMeshIndices)
				meshManager->ReleaseMeshIndex(static_cast<uint16_t>(meshIndex));

			m_GeometryEntries.clear();
			m_ChunkMeshIndices.clear();

			const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);
			const nvrhi::Format vertexFormat = Util::Geometry::GetVertexPositionFormat(m_VertexDesc);

			for (uint32_t k = 0; k < numChunks; k++) {
				const uint32_t start = k * trianglesPerChunk * 3u;
				const uint64_t indexOffset = m_IndexBuffer.m_Offset + static_cast<uint64_t>(start) * sizeof(uint16_t);
				const uint32_t meshIndex = sceneGraph->AllocateMeshIndex();
				m_ChunkMeshIndices.push_back(meshIndex);

				m_GeometryEntries.push_back({ MakeGeometryDesc(
					m_IndexBuffer.m_Buffer, indexOffset, trianglesPerChunk * 3u,
					m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, vertexStride, triShapeData.vertexCount,
					meshIndex, vertexFormat), AllocateGeometryIndex() });
			}

			MarkDirty(DirtyFlags::Mesh);
		}

		float3x4 rootTransform;
		XMStoreFloat3x4(&rootTransform, Util::Math::GetXMFromNiTransform(m_BSTriShape->world));

		for (uint32_t k = 0; k < m_GeometryEntries.size(); k++) {
			const uint32_t shapeIndex = (lookupData && k < lookupCount) ? lookupData[k] : k;
			if (shapeData && shapeIndex < shapeCount) {
				const auto& data = shapeData[shapeIndex];
				const float scale = (data.bound.w > 0.0001f) ? data.bound.w : 1.0f;
				float3x4 localTransform{
					data.transform[0].x * scale, data.transform[1].x * scale, data.transform[2].x * scale, data.bound.x,
					data.transform[0].y * scale, data.transform[1].y * scale, data.transform[2].y * scale, data.bound.y,
					data.transform[0].z * scale, data.transform[1].z * scale, data.transform[2].z * scale, data.bound.z
				};

				float3x4 instanceTransform;
				if (Util::Math::MatrixNearEqual(rootTransform, Constants::kIdentityTransform)) {
					instanceTransform = localTransform;
				} else {
					XMMATRIX localMat = XMLoadFloat3x4(&localTransform);
					XMMATRIX rootMat = XMLoadFloat3x4(&rootTransform);
					XMStoreFloat3x4(&instanceTransform, XMMatrixMultiply(localMat, rootMat));
				}

				const uint32_t meshIndex = m_ChunkMeshIndices[k];
				sceneGraph->WriteTransformData(meshIndex, instanceTransform, instanceTransform);
				meshManager->WritePropertiesData(static_cast<uint16_t>(meshIndex), m_Properties.GetData());
			}
		}

		m_Transform = rootTransform;
		m_PrevTransform = rootTransform;
		WriteTransform();
		WriteProperties();
	}

	const bool prevAlpha = m_Flags.all(Flags::Alpha);
	const bool alpha = m_Properties.IsAlpha();
	if (prevAlpha != alpha) {
		m_Flags.set(alpha, Flags::Alpha);
		for (auto& entry : m_GeometryEntries) {
			entry.desc.flags = alpha ? nvrhi::rt::GeometryFlags::None : nvrhi::rt::GeometryFlags::Opaque;
		}
		MarkDirty(DirtyFlags::Alpha);
	}
}
#endif
