#include "Core/Mesh/LandLODMesh.h"
#include "Renderer.h"
#include "Scene.h"
#include "Util.h"
#include "Types/RE/RE.h"

LandLODMesh::LandLODMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
	: Mesh(bsTriShape, commandList)
{
	if (!m_VertexBuffer.m_Buffer)
		return;

	m_Flags.set(Flags::LandLOD4);

	const auto slotIndex = m_VertexBuffer.m_Descriptor.Get();
	auto device = Renderer::GetSingleton()->GetDevice();
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(bsTriShape);
	const auto vertexStride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);
	const auto byteSize = static_cast<size_t>(triShapeData.vertexCount) * vertexStride;

	auto liveBufDesc = nvrhi::BufferDesc()
		.setByteSize(byteSize)
		.setCanHaveRawViews(true)
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - LandLOD", m_Name.c_str()));
		
	//const auto* rendererData = bsTriShape->GetGeometryRuntimeData().rendererData;

	m_LiveVertexBuffer = device->createBuffer(liveBufDesc);
	//commandList->writeBuffer(m_LiveVertexBuffer, Util::Adapter::GetVertexData(const_cast<RE::BSGraphics::TriShape*>(rendererData)), byteSize);

	commandList->copyBuffer(m_LiveVertexBuffer, 0, m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, byteSize);

	// Repoint the geometry desc to the live buffer for BLAS reads
	for (auto& entry : m_GeometryEntries) {
		entry.desc.geometryData.triangles.vertexBuffer = m_LiveVertexBuffer;
		entry.desc.geometryData.triangles.vertexOffset = 0;
	}

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
	Mesh::Update(commandList);

	auto multiBoundNode = Util::Adapter::AsMultiBoundNode(m_BSTriShape->parent);
	auto* multiBound = Util::Adapter::GetMultiBound(multiBoundNode);
	auto* aabb = Util::Adapter::GetMultiBoundAABB(multiBound);

	m_AABBCenter = { aabb->center.x, aabb->center.y };
#if defined(SKYRIM)
	m_AABBSize = { aabb->size.x, aabb->size.y };
#elif defined(FALLOUT4)
	m_AABBSize = { aabb->halfExtents.x * 2.0f, aabb->halfExtents.y * 2.0f };
#endif

	const float4 loadedRange = Util::Adapter::GetShaderManagerLoadedRange();

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
