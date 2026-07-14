#include "Core/SubIndexMesh.h"
#include "Core/SubIndexSegmentMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"
#include "interop/Triangle.hlsli"
#include "Util.h"

SubIndexMesh::SubIndexMesh(RE::BSSubIndexTriShape* triShape, SceneGraph* sceneGraph)
{
	m_BSTriShape = triShape;
	m_SceneGraph = sceneGraph;
	m_Name = MakeDebugName(triShape);
	m_Type = Type::SubIndex;

	const auto& geometryData = triShape->GetGeometryRuntimeData();
	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("SubIndexMesh::SubIndexMesh - No renderer data for {}", m_Name);
		return;
	}

	const auto& triShapeData = triShape->GetTrishapeRuntimeData();
	if (!ValidateCounts(triShapeData.triangleCount, triShapeData.vertexCount)) {
		logger::error("SubIndexMesh::SubIndexMesh - Failed to validate Triangle Count: {}, Vertex Count: {}",
			triShapeData.triangleCount, triShapeData.vertexCount);
		return;
	}

	m_VertexDesc = rendererData->vertexDesc;

	auto* triShapeDX12 = reinterpret_cast<RE::BSGraphics::TriShapeDX12*>(rendererData);
	auto* device = Renderer::GetSingleton()->GetDevice();

	// Create the shared nvrhi index buffer handle from the parent's D3D12 resource.
	// All K SubIndexSegmentMesh children will share this handle (different descriptor
	// slots, different indexOffset/count in their GeometryDesc).
	auto indexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(triShapeDX12->indexBufferDX12->GetDesc().Width)
		.setStructStride(sizeof(Triangle))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName("SubIndex Index Buffer");
	m_SharedIndexBuffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::D3D12_Resource,
		nvrhi::Object(triShapeDX12->indexBufferDX12),
		indexBufferDesc);

	// Create the shared nvrhi vertex buffer handle from the parent's D3D12 resource.
	auto vertexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(triShapeDX12->vertexBufferDX12->GetDesc().Width)
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName("SubIndex Vertex Buffer");
	m_SharedVertexBuffer = device->createHandleForNativeBuffer(
		nvrhi::ObjectTypes::D3D12_Resource,
		nvrhi::Object(triShapeDX12->vertexBufferDX12),
		vertexBufferDesc);

	// m_GeometryDescs is intentionally left empty: the SubIndexMesh itself does not
	// contribute geometry to any cluster. The K SubIndexSegmentMesh children each
	// contribute their own 1-element GeometryDesc to their own cluster.
}

void SubIndexMesh::Update(nvrhi::ICommandList* commandList)
{
	// Guard against the dtor hook (which sets m_BSTriShape = null) racing with the
	// traversal callback. If the parent is gone, skip the inherited update entirely.
	if (!m_BSTriShape)
		return;

	BaseMesh::Update(commandList);

	auto* triShape = m_BSTriShape;
	auto* subIndexShape = Util::Adapter::AsSubIndexTriShape(triShape);
	if (!subIndexShape)
		return;

	const bool bypass = Scene::GetSingleton()->g_BypassSubIndexVisibility && *Scene::GetSingleton()->g_BypassSubIndexVisibility;

	const auto& triShapeData = triShape->GetTrishapeRuntimeData();
	const auto& runtimeData = subIndexShape->GetSubIndexedTrishapeRuntimeData();

	// Track currently-visible segment indices for the destruction sweep.
	eastl::hash_set<uint32_t> currentlyVisible;
	currentlyVisible.reserve(runtimeData.numSegments);

	for (size_t i = 0; i < runtimeData.numSegments; i++) {
		const auto& segment = runtimeData.segmentData[i];

		const bool visible = bypass || (segment.flags != 0u);
		if (!visible || segment.numTris == 0)
			continue;

		const auto finalIndex = segment.index + (segment.numTris * 3u);
		if (finalIndex > triShapeData.triangleCount * 3u) {
			logger::warn("SubIndexMesh::Update - Segment {} index {} exceeds the maximum of {}",
				i, finalIndex, triShapeData.triangleCount * 3u);
			continue;
		}

		currentlyVisible.insert(static_cast<uint32_t>(i));

		if (m_SegmentMap.find(static_cast<uint32_t>(i)) == m_SegmentMap.end())
			CreateSegment(static_cast<uint32_t>(i));
	}

	// Destroy segments that are no longer visible.
	for (auto it = m_SegmentMap.begin(); it != m_SegmentMap.end(); ) {
		if (!currentlyVisible.contains(it->first)) {
			DestroySegment(it->first);
			it = m_SegmentMap.erase(it);
		} else {
			++it;
		}
	}

	// Update surviving segments (so they read fresh world transforms / refresh material).
	for (auto& seg : m_Segments) {
		seg->Update(commandList);
	}
}

void SubIndexMesh::CreateSegment(uint32_t segmentIndex)
{
	// m_BSTriShape is known to be a BSSubIndexTriShape here (Update verified via
	// AsSubIndexTriShape before calling CreateSegment). The cast is safe.
	auto* subIndexShape = static_cast<RE::BSSubIndexTriShape*>(m_BSTriShape);

	// Pass `this` as a raw back-pointer. Lifetime is safe: the SubIndexMesh owns the
	// SubIndexSegmentMesh via unique_ptr in m_Segments, so the segment is destroyed
	// before the manager.
	auto segMesh = eastl::make_unique<SubIndexSegmentMesh>(
		this,
		subIndexShape,
		segmentIndex,
		nullptr);

	auto* rawSeg = segMesh.get();

	// Create a per-segment cluster in SceneGraph::m_SubIndexSegmentClusters and add
	// the segment to it. Each segment gets its own BLAS / InstanceData / TLAS entry.
	auto* cluster = m_SceneGraph->GetOrCreateSegmentCluster(rawSeg, m_Owner);
	cluster->AddMember(rawSeg);
	m_SceneGraph->MarkClusterDirty(cluster);

	m_Segments.push_back(eastl::move(segMesh));
	m_SegmentMap[segmentIndex] = rawSeg;
}

void SubIndexMesh::DestroySegment(uint32_t segmentIndex)
{
	auto it = m_SegmentMap.find(segmentIndex);
	if (it == m_SegmentMap.end())
		return;

	auto* segMesh = it->second;

	// Remove the segment from its cluster and erase the cluster from
	// SceneGraph::m_SubIndexSegmentClusters. The cluster is destroyed here.
	m_SceneGraph->RemoveSegmentCluster(segMesh);

	// Erase the unique_ptr from m_Segments (which destroys the segment, releasing
	// its two descriptor handles back to the bindless tables).
	for (auto segIt = m_Segments.begin(); segIt != m_Segments.end(); ++segIt) {
		if (segIt->get() == segMesh) {
			m_Segments.erase(segIt);
			break;
		}
	}
}

void SubIndexMesh::DestroyAllSegments()
{
	// Snapshot indices before invalidation.
	eastl::vector<uint32_t> indices;
	indices.reserve(m_SegmentMap.size());
	for (auto& [k, _] : m_SegmentMap)
		indices.push_back(k);

	for (auto idx : indices)
		DestroySegment(idx);

	m_SegmentMap.clear();
	m_Segments.clear();
}

void SubIndexMesh::OnDestroy()
{
	// Inherited behavior: null m_BSTriShape so subsequent BaseMesh::Update is a no-op
	// and the manager is recognized as "destroyed" by SceneGraph::OnDestroy.
	BaseMesh::OnDestroy();

	// Destroy all K segments (removes each from its cluster and m_SubIndexSegmentClusters,
	// then deletes the SubIndexSegmentMesh). The SubIndexMesh itself is deferred-destroyed
	// by SceneGraph's pending-destroy flow after the GPU fence resolves.
	DestroyAllSegments();
}
