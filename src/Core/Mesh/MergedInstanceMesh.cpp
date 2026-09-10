#include "Core/Mesh/MergedInstanceMesh.h"
#include "Core/MeshManager.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"
#include "Types.h"
#include "Types/RE/RE.h"

#include <cmath>
#include <limits>

using namespace DirectX;

namespace
{
	DirectX::XMMATRIX BuildInstanceMatrix(const DirectX::XMMATRIX& a_shapeLocal, const RE::GrassInstanceData& a_record, bool a_uniformScale)
	{
		const float heightScale = static_cast<float>(a_record.heightScale);
		const float3 scaleMask = a_uniformScale ? float3(1.0f, 1.0f, 1.0f) : float3(0.0f, 0.0f, 1.0f);
		const float3 axisScale = heightScale * scaleMask + float3(1.0f, 1.0f, 1.0f);

		const float3 row0(a_record.rot1.x, a_record.rot1.y, a_record.rot1.z);
		const float3 row1(a_record.rot2.y, a_record.rot2.z, a_record.rot3.x);
		const float3 row2(a_record.rot3.z, a_record.rot2.x, a_record.rot3.y);
		const float3 translation(a_record.position.x, a_record.position.y, a_record.position.z);

		float3x4 instanceTransform(
			row0.x * axisScale.x, row0.y * axisScale.y, row0.z * axisScale.z, translation.x,
			row1.x * axisScale.x, row1.y * axisScale.y, row1.z * axisScale.z, translation.y,
			row2.x * axisScale.x, row2.y * axisScale.y, row2.z * axisScale.z, translation.z);

		return XMMatrixMultiply(a_shapeLocal, XMLoadFloat3x4(&instanceTransform));
	}
}

MergedInstanceMesh::MergedInstanceMesh(
	RE::BSTriShape* a_sourceShape,
	const eastl::vector<RE::GrassInstanceData>& a_instances,
	bool a_uniformScale,
	[[maybe_unused]] nvrhi::ICommandList* a_commandList)
{
	m_Name = MakeDebugName(a_sourceShape);
	m_BSTriShape = a_sourceShape;
	m_Type = Type::Grass;

	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(a_sourceShape);
	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("MergedInstanceMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(a_sourceShape);
	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount))
		return;

	if (a_instances.empty())
		return;

	m_VertexDesc = rendererData->vertexDesc;

	m_IndexBuffer = CreateIndexBuffer(rendererData);
	m_VertexBuffer = CreateVertexBuffer(rendererData);
	if (!m_IndexBuffer.m_Buffer || !m_VertexBuffer.m_Buffer) {
		logger::warn("MergedInstanceMesh - No GPU index/vertex buffer for {}", m_Name);
		return;
	}

	const uint32_t indexCount = static_cast<uint32_t>(triShapeData.triangleCount) * 3;
	const uint16_t stride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);
	const nvrhi::Format vertexFormat = Util::Geometry::GetVertexPositionFormat(m_VertexDesc);

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	auto* meshManager = sceneGraph->GetMeshManager().get();
	auto* grassTransformBuffer = meshManager->GetGrassTransformBuffer();

	const uint32_t count = static_cast<uint32_t>(a_instances.size());
	const uint32_t transformBase = meshManager->AllocateGrassTransforms(count);
	if (transformBase == UINT32_MAX) {
		logger::warn("MergedInstanceMesh - Grass transform pool exhausted for {} ({} instances)", m_Name, count);
		return;
	}

	const auto shapeLocal = Util::Math::GetXMFromNiTransform(a_sourceShape->local);

	float3 boundMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	float3 boundMax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());

	for (uint32_t i = 0; i < count; ++i)
	{
		const auto world = BuildInstanceMatrix(shapeLocal, a_instances[i], a_uniformScale);

		float3x4 xf;
		XMStoreFloat3x4(&xf, world);
		meshManager->WriteGrassTransform(transformBase + i, xf, xf);

		const float3 position(a_instances[i].position.x, a_instances[i].position.y, a_instances[i].position.z);
		const XMVECTOR worldPosition = XMVector3Transform(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&position)), world);
		XMFLOAT3 fp;
		XMStoreFloat3(&fp, worldPosition);
		boundMin = float3(std::min(boundMin.x, fp.x), std::min(boundMin.y, fp.y), std::min(boundMin.z, fp.z));
		boundMax = float3(std::max(boundMax.x, fp.x), std::max(boundMax.y, fp.y), std::max(boundMax.z, fp.z));
	}

	m_GrassTransformBase = transformBase;
	m_GrassInstanceCount = count;

	// All blade geometries share the source index/vertex buffers and a single mesh slot.
	AllocateMeshIndex();
	const uint16_t geometryIndex = AllocateGeometryIndex();

	m_GeometryEntries.reserve(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		GeometryEntry entry;
		entry.desc = MakeGeometryDesc(
			m_IndexBuffer.m_Buffer, m_IndexBuffer.m_Offset, indexCount,
			m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, stride, triShapeData.vertexCount,
			grassTransformBuffer, static_cast<uint64_t>(transformBase + i) * sizeof(TransformData),
			vertexFormat);
		entry.geometryIndex = (i == 0) ? geometryIndex : UINT16_MAX;
		m_GeometryEntries.push_back(entry);
	}

	CreateMaterial();

	// Pad the bounds by the blade radius (the group AABB is only used for culling).
	const float bladeRadius = Util::Adapter::GetNiBoundRadius(a_sourceShape->worldBound);
	m_WorldBound.center = RE::NiPoint3(
		(boundMin.x + boundMax.x) * 0.5f,
		(boundMin.y + boundMax.y) * 0.5f,
		(boundMin.z + boundMax.z) * 0.5f);
	const float3 extent = boundMax - boundMin;
	m_WorldBound.radius = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z) * 0.5f + bladeRadius;

	m_Transform = Constants::kIdentityTransform;
	m_PrevTransform = Constants::kIdentityTransform;
	m_World = RE::NiTransform();
	m_NeedsPrevInit = false;
}

MergedInstanceMesh::~MergedInstanceMesh()
{
	if (m_GrassTransformBase != UINT32_MAX)
		Scene::GetSingleton()->GetSceneGraph()->GetMeshManager()->ReleaseGrassTransforms(m_GrassTransformBase, m_GrassInstanceCount);
}

void MergedInstanceMesh::Update([[maybe_unused]] nvrhi::ICommandList* a_commandList)
{
	if (!m_BSTriShape)
		return;

	m_Properties.Update(m_BSTriShape, m_Flags.all(Flags::Eyes));
	WriteProperties();

	// Per-blade transforms live in the grass transform pool; the mesh transform stays identity.
	m_World = RE::NiTransform();
	m_Transform = Constants::kIdentityTransform;
	m_PrevTransform = Constants::kIdentityTransform;
	m_NeedsPrevInit = false;

	WriteTransform();

	const bool prevAlpha = m_Flags.all(Flags::Alpha);
	const bool alpha = m_Properties.IsAlpha();
	if (prevAlpha != alpha)
	{
		m_Flags.set(alpha, Flags::Alpha);

		for (auto& entry : m_GeometryEntries)
			entry.desc.flags = alpha ? nvrhi::rt::GeometryFlags::None : nvrhi::rt::GeometryFlags::Opaque;

		MarkDirty(DirtyFlags::Alpha);
	}

	UpdateMaterial();
}
