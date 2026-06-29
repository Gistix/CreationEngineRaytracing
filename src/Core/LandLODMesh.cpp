#include "Core/LandLODMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "Util.h"

LandLODMesh::LandLODMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
	: DirectMesh(bsTriShape, commandList)
{
	if (!m_VertexBuffer.m_Buffer)
		return;

	m_Flags.set(Flags::LandLOD4);

	const auto slot = m_VertexBuffer.m_Descriptor.Get();
	auto device = Renderer::GetSingleton()->GetDevice();
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	const auto& triShapeData = bsTriShape->GetTrishapeRuntimeData();
	const auto vertexStride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);
	const auto byteSize = static_cast<size_t>(triShapeData.vertexCount) * vertexStride;

	auto liveBufDesc = nvrhi::BufferDesc()
		.setByteSize(byteSize)
		.setCanHaveRawViews(true)
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true);
	auto liveBuffer = device->createBuffer(liveBufDesc);
	m_LiveVertexBuffer = liveBuffer;

	const auto* rendererData = bsTriShape->GetGeometryRuntimeData().rendererData;
	commandList->writeBuffer(liveBuffer, Util::Adapter::GetVertexData(const_cast<RE::BSGraphics::TriShape*>(rendererData)), byteSize);

	// Repoint the geometry desc to the live buffer for BLAS reads
	for (auto& desc : GetGeometryDescsMutable())
		desc.geometryData.triangles.vertexBuffer = liveBuffer;

	device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
		nvrhi::BindingSetItem::RawBuffer_SRV(slot, liveBuffer));
	device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_SRV(slot, liveBuffer));
	device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_UAV(slot, liveBuffer));
}

bool LandLODMesh::Update(BLASCluster* cluster)
{
	bool changed = DirectMesh::Update(cluster);

	if (!m_BSTriShape)
		return changed;

	for (auto* node = static_cast<RE::NiAVObject*>(m_BSTriShape->parent); node; node = node->parent) {
		if (auto* multiBoundNode = netimmerse_cast<RE::BSMultiBoundNode*>(node)) {
			auto& runtimeData = multiBoundNode->GetRuntimeData();
			auto* multiBound = runtimeData.multiBound.get();
			if (!multiBound || !multiBound->data)
				break;

			auto* aabb = netimmerse_cast<RE::BSMultiBoundAABB*>(multiBound->data.get());
			if (!aabb)
				break;

			m_AABBCenter = { aabb->center.x, aabb->center.y };
			m_AABBSize = { aabb->size.x, aabb->size.y };
			break;
		}
	}

	float4 loadedRange = *reinterpret_cast<const float4*>(&RE::BSShaderManager::State::GetSingleton().loadedRange);

	const float2 loadedPosition = { loadedRange.x , loadedRange.y };
	const float2 loadedSize = { loadedRange.z * 2.0f , loadedRange.w * 2.0f };

	m_PrevIntersecting = m_Intersecting;
	m_Intersecting = Util::Math::Intersects(loadedPosition, loadedSize, m_AABBCenter, m_AABBSize);

	if (m_Intersecting || m_PrevIntersecting)	
		UpdateOcclusion(cluster->GetInstanceTransform());

	return changed;
}

void LandLODMesh::UpdateOcclusion(const float3x4& clusterTransform)
{
	const auto& descs = GetGeometryDescs();
	if (!descs.empty()) {
		const auto& firstDesc = descs[0];

		LandLODUpdate update(
			GetVertexID(),
			firstDesc.geometryData.triangles.vertexCount,
			firstDesc.geometryData.triangles.vertexStride,
			GetLocalToOwner(),
			clusterTransform);

		auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
		sceneGraph->GetLandLODMeshUpdates()[this] = update;

		MarkDirty(DirtyFlags::Vertex);
	}
}
