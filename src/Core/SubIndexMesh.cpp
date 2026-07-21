#include "Core/SubIndexMesh.h"
#include "Core/SubIndexSegmentMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"
#include "interop/Triangle.hlsli"
#include "Util.h"

SubIndexMesh::SubIndexMesh(RE::BSSubIndexTriShape* triShape)
{
	m_BSTriShape = triShape;
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
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	// Create the shared nvrhi index buffer handle from the parent's D3D12 resource.
	// All K SubIndexSegmentMesh children will share this handle (different descriptor
	// slots, different indexOffset/count in their GeometryDesc).
	auto indexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(triShapeDX12->indexBufferDX12->GetDesc().Width)
		.setStructStride(sizeof(Triangle))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName("SubIndex Index Buffer");

	m_IndexBuffer = device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->indexBufferDX12), indexBufferDesc);

	m_IndexDescriptor = sceneGraph->GetTriangleDescriptors()->m_DescriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_IndexBuffer));

	// Create the shared nvrhi vertex buffer handle from the parent's D3D12 resource.
	auto vertexBufferDesc = nvrhi::BufferDesc()
		.setByteSize(triShapeDX12->vertexBufferDX12->GetDesc().Width)
		.setCanHaveRawViews(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName("SubIndex Vertex Buffer");

	m_VertexBuffer = device->createHandleForNativeBuffer(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(triShapeDX12->vertexBufferDX12), vertexBufferDesc);

	m_VertexDescriptor = sceneGraph->GetVertexDescriptors()->m_DescriptorTable->CreateDescriptorHandle(nvrhi::BindingSetItem::RawBuffer_SRV(0, m_VertexBuffer));

	AllocateTransformIndex();

	CreateMaterial();
}

void SubIndexMesh::SetHidden(bool hidden)
{
	BaseMesh::SetHidden(hidden);

	// Propagate to segments
	for (auto& seg : m_Segments) {
		seg->SetHidden(hidden);
	}
}

void SubIndexMesh::Update(nvrhi::ICommandList* commandList)
{
	BaseMesh::Update(commandList);

	auto* triShape = m_BSTriShape;
	auto* subIndexShape = Util::Adapter::AsSubIndexTriShape(triShape);
	if (!subIndexShape)
		return;

	const bool bypassVisibility = *Scene::GetSingleton()->g_BypassSubIndexVisibility;

	const auto& triShapeData = triShape->GetTrishapeRuntimeData();
	const auto& runtimeData = subIndexShape->GetSubIndexedTrishapeRuntimeData();

	// Clear stale dirty flags and copy fresh world state from the manager to existing
	// segments. Must run before the visibility loop so MarkDirty(Visibility) set by
	// SetSubIndexHidden survives to Phase E2.
	for (auto& seg : m_Segments) {
		seg->SyncFrom(this);
	}

	eastl::unordered_set<uint64_t> visitedKeys;
	visitedKeys.reserve(runtimeData.numSegments);

	for (size_t i = 0; i < runtimeData.numSegments; i++) {
		const auto& segment = runtimeData.segmentData[i];

		const bool firstSegment = (i == 0);

		const uint32_t start = segment.index;
		const uint32_t numTris = (firstSegment ? segment.numTris : segment.unkTriCount);
		const uint8_t flags = (firstSegment ? segment.flags : segment.unkFlags);

		if (numTris == 0)
			continue;

		const uint32_t end = start + numTris * 3u;
		if (end > triShapeData.triangleCount * 3u) {
			logger::warn("SubIndexMesh::Update - Segment {} index {} exceeds the maximum of {}", i, end, triShapeData.triangleCount * 3u);
			continue;
		}

		const bool visible = bypassVisibility || (flags != 0u);

		const uint64_t key = MakeSegmentKey(start, numTris);
		visitedKeys.insert(key);

		auto it = m_SegmentMap.find(key);
		if (it == m_SegmentMap.end()) {
			// New segment: create only if currently visible. Hidden segments
			// (e.g. engine flag = 0) don't need an entry — when the flag flips
			// on, a future Update will see a missing key and create it then.
			if (visible)
				CreateSegment(start, numTris);
		} else {
			// Existing segment: just toggle its SubIndexHidden flag.
			it->second->SetSubIndexHidden(!visible);
		}
	}

	// Hide orphaned segments whose identity changed in the engine's segment data
	// (e.g. start index or numTris changed → key differs). They remain in the map
	// for reuse if the engine flips the key back.
	for (auto& [key, segMesh] : m_SegmentMap) {
		if (!visitedKeys.contains(key))
			segMesh->SetSubIndexHidden(true);
	}
}

void SubIndexMesh::CreateSegment(uint32_t start, uint32_t numTris)
{
	// m_BSTriShape is known to be a BSSubIndexTriShape here (Update verified via
	// AsSubIndexTriShape before calling CreateSegment). The cast is safe.
	auto* subIndexShape = static_cast<RE::BSSubIndexTriShape*>(m_BSTriShape);

	// Pass `this` as a raw back-pointer. Lifetime is safe: the SubIndexMesh owns the
	// SubIndexSegmentMesh via unique_ptr in m_Segments, so the segment is destroyed
	// before the manager.
	auto segMesh = eastl::make_unique<SubIndexSegmentMesh>(this, subIndexShape, start, numTris);

	auto* rawSeg = segMesh.get();

	// Copy world state from the manager (SyncSegments already cleared old segments
	// before the visibility loop; new segments get their world state here).
	rawSeg->SyncFrom(this);

	// Create a per-segment cluster in SceneGraph::m_SubIndexSegmentClusters and add
	// the segment to it. Each segment gets its own BLAS / InstanceData / TLAS entry.
	// The cluster lives in m_SubIndexSegmentClusters for the lifetime of the segment
	// (the segment is never destroyed unless the parent is destroyed), so the cluster's
	// hash-map address is stable → iteration order in Phase E1 is stable.
	auto sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	auto* cluster = sceneGraph->GetOrCreateSegmentCluster(rawSeg, m_Owner);
	cluster->AddMember(rawSeg);
	sceneGraph->MarkClusterDirty(cluster);

	const uint64_t key = MakeSegmentKey(start, numTris);
	m_Segments.push_back(eastl::move(segMesh));
	m_SegmentMap[key] = rawSeg;
}

void SubIndexMesh::DestroyAllSegments()
{
	for (auto& segmentMesh: m_Segments)
	{
		segmentMesh->GetCluster()->RemoveMember(segmentMesh.get());
	}

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
