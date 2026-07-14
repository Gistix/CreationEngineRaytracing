#include "Core/SubIndexSegmentMesh.h"
#include "Core/SubIndexMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"
#include "Util.h"
#include "interop/Triangle.hlsli"

SubIndexSegmentMesh::SubIndexSegmentMesh(SubIndexMesh* manager, RE::BSSubIndexTriShape* parent, uint32_t start, uint32_t numTris)
{
	m_BSTriShape = parent;  // so BaseMesh::Update reads parent's world/previousWorld/worldBound
	m_Type = Type::SubIndex;
	m_Name = std::format("{} [start={} tris={}]", MakeDebugName(parent).c_str(), start, numTris).c_str();

	m_Manager = manager;
	m_Start = start;
	m_NumTris = numTris;

	m_VertexDesc = manager->GetVertexDesc();

	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);
	const auto& triShapeData = parent->GetTrishapeRuntimeData();

	m_GeometryDescs.push_back(MakeGeometryDesc(
		manager->GetIndexBuffer(), start, numTris * 3u,
		manager->GetVertexBuffer(), vertexStride, triShapeData.vertexCount));

	CreateMaterial();
}

uint16_t SubIndexSegmentMesh::GetIndexID(size_t geometryIndex) const
{
	return m_Manager->GetIndexID(geometryIndex);
}

uint16_t SubIndexSegmentMesh::GetVertexID() const
{
	return m_Manager->GetVertexID();
}