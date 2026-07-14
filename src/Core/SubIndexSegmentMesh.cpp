#include "Core/SubIndexSegmentMesh.h"
#include "Core/SubIndexMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"
#include "Util.h"
#include "interop/Triangle.hlsli"

SubIndexSegmentMesh::SubIndexSegmentMesh(
	SubIndexMesh* manager,
	RE::BSSubIndexTriShape* parent,
	uint32_t segmentIndex,
	[[maybe_unused]] nvrhi::ICommandList* commandList)
{
	m_Manager = manager;
	m_BSTriShape = parent;  // so BaseMesh::Update reads parent's world/previousWorld/worldBound
	m_Type = Type::SubIndex;
	m_Name = std::format("{} [seg {}]", MakeDebugName(parent).c_str(), segmentIndex).c_str();
	m_VertexDesc = manager->GetVertexDesc();
	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);

	auto* sceneGraph = manager->GetSceneGraph();

	// Unique descriptor for this segment in the Triangles[] bindless table.
	// Shares the manager's nvrhi buffer handle (no separate wrapping needed).
	m_IndexDescriptor = sceneGraph->GetTriangleDescriptors()->m_DescriptorTable
		->CreateDescriptorHandle(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, manager->GetSharedIndexBuffer()));

	// Unique descriptor for this segment in the Vertices[] bindless table.
	m_VertexDescriptor = sceneGraph->GetVertexDescriptors()->m_DescriptorTable
		->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, manager->GetSharedVertexBuffer()));

	const auto& triShapeData = parent->GetTrishapeRuntimeData();
	const auto& segment = parent->GetSubIndexedTrishapeRuntimeData().segmentData[segmentIndex];

	// The geometry desc references the manager's shared nvrhi handles by raw pointer.
	// Lifetime is guaranteed because the manager outlives the segment (owned by it).
	m_GeometryDescs.push_back(MakeGeometryDesc(
		manager->GetSharedIndexBuffer().Get(),
		segment.index, segment.numTris * 3u,
		manager->GetSharedVertexBuffer().Get(),
		vertexStride, triShapeData.vertexCount));

	CreateMaterial();
}

void SubIndexSegmentMesh::DetachFromCluster()
{
	if (auto* cluster = GetCluster()) {
		cluster->RemoveMember(this);
		SetCluster(nullptr);
	}
}

void SubIndexSegmentMesh::Update(nvrhi::ICommandList* commandList)
{
	// Inherited BaseMesh::Update reads m_BSTriShape (the parent) for world/previousWorld/worldBound.
	// If the parent has been destroyed (m_BSTriShape is null in the manager), this is a no-op.
	BaseMesh::Update(commandList);
}
