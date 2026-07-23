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

	const auto slotIndex = m_VertexBuffer.m_Descriptor.Get();
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
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - LandLOD", m_Name.c_str()));
		
	//const auto* rendererData = bsTriShape->GetGeometryRuntimeData().rendererData;

	m_LiveVertexBuffer = device->createBuffer(liveBufDesc);
	//commandList->writeBuffer(m_LiveVertexBuffer, Util::Adapter::GetVertexData(const_cast<RE::BSGraphics::TriShape*>(rendererData)), byteSize);

	commandList->copyBuffer(m_LiveVertexBuffer, 0, m_VertexBuffer.m_Buffer, 0, byteSize);

	// Repoint the geometry desc to the live buffer for BLAS reads
	for (auto& entry : m_GeometryEntries)
		entry.desc.geometryData.triangles.vertexBuffer = m_LiveVertexBuffer;

	// RT shaders read from here
	device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
		nvrhi::BindingSetItem::RawBuffer_SRV(slotIndex, m_LiveVertexBuffer));

	// LandLODOccluder input
	device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_SRV(slotIndex, m_VertexBuffer.m_Buffer));

	// LandLODOccluder output
	device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_UAV(slotIndex, m_LiveVertexBuffer));
}

void LandLODMesh::Update(nvrhi::ICommandList* commandList)
{
	DirectMesh::Update(commandList);

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
		UpdateOcclusion();
}

void LandLODMesh::UpdateOcclusion()
{
	if (m_GeometryEntries.empty()) {
		logger::info("LandLODMesh::UpdateOcclusion - No geometry");
		return;
	}

	const auto& firstDesc = m_GeometryEntries[0].desc;

	LandLODUpdate update(
		GetVertexID(),
		firstDesc.geometryData.triangles.vertexCount,
		firstDesc.geometryData.triangles.vertexStride,
		m_Transform);

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();
	sceneGraph->GetLandLODMeshUpdates()[this] = update;

	MarkDirty(DirtyFlags::Vertex);
}
