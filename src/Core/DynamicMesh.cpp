#include "Core/DynamicMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Types/RE/RE.h"

DynamicMesh::DynamicMesh(RE::BSDynamicTriShape* bsDynamicTriShape, nvrhi::ICommandList* commandList) :
	SkinnedMesh()
{
	m_Name = MakeDebugName(bsDynamicTriShape);
	m_BSTriShape = bsDynamicTriShape;
	m_Type = Type::Dynamic;

	auto device = Renderer::GetSingleton()->GetDevice();

	auto runtimeData = Util::Adapter::GetDynamicTrishapeRuntimeData(bsDynamicTriShape);

	if (runtimeData.dataSize == 0) {
		logger::warn("DynamicMesh::DynamicMesh - No dynamic data for {}", m_Name);
		return;
	}

	auto geometryData = Util::Adapter::GetGeometryRuntimeData(bsDynamicTriShape);

	auto* skinInstance = geometryData.skinInstance;
	if (!skinInstance) {
		logger::warn("DynamicMesh::DynamicMesh - No skin instance for {}", m_Name);
		return;
	}

#if defined(SKYRIM)
	const auto& skinPartition = static_cast<RE::NiSkinInstance*>(skinInstance)->skinPartition;
	if (!skinPartition || skinPartition->numPartitions == 0) {
		logger::warn("DynamicMesh::DynamicMesh - No skin partitions for {}", m_Name);
		return;
	}

	auto* basePartitionBuffer = skinPartition->partitions[0].buffData;
	if (!basePartitionBuffer) {
		logger::warn("DynamicMesh::DynamicMesh - No base partition buffer for {}", m_Name);
		return;
	}

	const uint32_t vertexCount = skinPartition->vertexCount;

	if (!ValidateCounts(skinPartition->partitions[0].triangles, vertexCount))
		return;

	m_VertexBuffer = CreateVertexBuffer(basePartitionBuffer);
	if (!m_VertexBuffer.m_Buffer)
		return;

	AllocateMeshIndex();
	m_VertexCount = vertexCount;

	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(basePartitionBuffer->vertexDesc);

	CreateSkinningBuffers(commandList, basePartitionBuffer, vertexCount, vertexStride);
#elif defined(FALLOUT4)
	auto* rendererData = geometryData.rendererData;
	if (!rendererData) {
		logger::warn("DynamicMesh::DynamicMesh - No renderer data for {}", m_Name);
		return;
	}

	const uint32_t vertexCount = Util::Adapter::GetVertexCount(bsDynamicTriShape);
	const uint32_t triangleCount = Util::Adapter::GetTriangleCount(bsDynamicTriShape);
	if (!ValidateCounts(triangleCount, vertexCount))
		return;

	m_VertexBuffer = CreateVertexBuffer(rendererData);
	if (!m_VertexBuffer.m_Buffer)
		return;

	AllocateMeshIndex();
	m_VertexCount = vertexCount;

	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(geometryData.vertexDesc);

	CreateSkinningBuffers(commandList, rendererData, vertexCount, vertexStride);
#endif

	m_DynamicData.resize(runtimeData.dataSize, 0u);

	runtimeData.lock->Lock();
	UpdateDynamicData(runtimeData.dynamicData, runtimeData.dataSize);
	runtimeData.lock->Unlock();

	auto liveBufferDesc = nvrhi::BufferDesc()
		.setByteSize(2ull * runtimeData.dataSize)
		.setStructStride(sizeof(float4))
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - Dynamic (Live)", m_Name.c_str()));

	m_DynamicBuffer = device->createBuffer(liveBufferDesc);

	auto originalBufferDesc = nvrhi::BufferDesc()
		.setByteSize(runtimeData.dataSize)
		.setStructStride(sizeof(float4))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setDebugName(std::format("{} - Dynamic (Original)", m_Name.c_str()));

	m_OriginalDynamicBuffer = device->createBuffer(originalBufferDesc);

	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	m_DynamicDescriptor = sceneGraph->GetDynamicVertexReadDescriptors()->m_DescriptorTable->CreateDescriptorHandle(
		nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_OriginalDynamicBuffer));

	device->writeDescriptorTable(
		sceneGraph->GetDynamicVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_UAV(m_DynamicDescriptor.Get(), m_DynamicBuffer));

	device->writeDescriptorTable(
		sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_SRV(m_DynamicDescriptor.Get(), m_DynamicBuffer));

#if defined(FALLOUT4)
	// Single index buffer + geometry entry for FO4 dynamic meshes (no skin partitions).
	auto* rendererData = geometryData.rendererData;
	auto indexBuffer = CreateIndexBuffer(rendererData);
	if (indexBuffer.m_Buffer) {
		const uint32_t vertexCount = Util::Adapter::GetVertexCount(bsDynamicTriShape);
		const uint32_t triangleCount = Util::Adapter::GetTriangleCount(bsDynamicTriShape);
		m_IndexBuffers.push_back(std::move(indexBuffer));
		m_GeometryEntries.push_back({
			MakeGeometryDesc(m_IndexBuffers[0].m_Buffer, 0, triangleCount * 3,
				m_DynamicBuffer, static_cast<uint16_t>(sizeof(float4)), vertexCount, GetMeshIndex()),
			AllocateGeometryIndex()
		});
		m_GeometryPartitionIndices.push_back(0);
		RefreshVisibleGeometryCache();
	}
#endif

	BuildSkinned(bsDynamicTriShape, m_DynamicBuffer, static_cast<uint16_t>(sizeof(float4)), false);

	CreateMaterial();

	InitSkinToBones(skinInstance);

	InitDismemberSkin(skinInstance);
}

void DynamicMesh::UpdateDynamicData(void* dynamicData, uint32_t dataSize)
{
	if (std::memcmp(m_DynamicData.data(), dynamicData, dataSize) == 0)
		return;

	std::memcpy(m_DynamicData.data(), dynamicData, dataSize);

	m_NeedsUpload = true;
}

void DynamicMesh::Update(nvrhi::ICommandList* commandList)
{
	SkinnedMesh::Update(commandList);

	if (!m_NeedsUpload)
		return;

	// Upload the latest morph positions to the skinning input; the skinning pass produces the live buffer.
	// Static mutex serializes concurrent uploads from parallel worker threads.
	{
		static std::mutex uploadMutex;
		std::scoped_lock lock(uploadMutex);
		commandList->writeBuffer(m_OriginalDynamicBuffer, m_DynamicData.data(), m_DynamicData.size());
	}

	MarkDirty(DirtyFlags::Vertex);

	m_NeedsUpload = false;
}
