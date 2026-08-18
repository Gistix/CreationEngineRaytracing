#include "Core/Mesh/DynamicMesh.h"
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

	const uint32_t dynamicDataSize = Util::Adapter::GetDynamicDataSize(bsDynamicTriShape);

	if (dynamicDataSize == 0) {
		logger::warn("DynamicMesh::DynamicMesh - No dynamic data for {}", m_Name);
		return;
	}

	const auto& geometryData = Util::Adapter::GetGeometryRuntimeData(bsDynamicTriShape);

#if defined(SKYRIM)
	auto* skinInstance = geometryData.skinInstance;
	if (!skinInstance) {
		logger::warn("DynamicMesh::DynamicMesh - No skin instance for {}", m_Name);
		return;
	}

	const auto& skinPartition = skinInstance->skinPartition;
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

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(bsDynamicTriShape);
	const uint32_t vertexCount = triShapeData.vertexCount;

	if (!ValidateCounts(triShapeData.triangleCount, vertexCount))
		return;

	m_VertexBuffer = CreateVertexBuffer(rendererData);
	if (!m_VertexBuffer.m_Buffer)
		return;

	AllocateMeshIndex();

	m_VertexCount = vertexCount;

	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(rendererData->vertexDesc);

	CreateSkinningBuffers(commandList, rendererData, vertexCount, vertexStride);
#endif

	// Dynamic positions are float4 per vertex; the BLAS reads them as RGB32_FLOAT with a float4
	// stride so the trailing w component is skipped.
	m_DynamicData.resize(dynamicDataSize, 0u);

	Util::Adapter::UpdateDynamicData(this, bsDynamicTriShape);

	// Live (skinning output) positions; BLAS source. Sized for two vertex sets:
	// current positions in [0, vertexCount), previous-frame positions in [vertexCount, 2 * vertexCount).
	auto liveBufferDesc = nvrhi::BufferDesc()
		.setByteSize(2ull * dynamicDataSize)
		.setStructStride(sizeof(float4))
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - Dynamic (Live)", m_Name.c_str()));

	m_DynamicBuffer = device->createBuffer(liveBufferDesc);

	// Original (rest/morph) positions copied from the game each frame; skinning input.
	auto originalBufferDesc = nvrhi::BufferDesc()
		.setByteSize(dynamicDataSize)
		.setStructStride(sizeof(float4))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setDebugName(std::format("{} - Dynamic (Original)", m_Name.c_str()));

	m_OriginalDynamicBuffer = device->createBuffer(originalBufferDesc);

	// Register at a shared dynamic slot: original -> SRV (input), live -> UAV (output).
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	m_DynamicDescriptor = sceneGraph->GetDynamicVertexReadDescriptors()->m_DescriptorTable->CreateDescriptorHandle(
		nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_OriginalDynamicBuffer));

	device->writeDescriptorTable(
		sceneGraph->GetDynamicVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_UAV(m_DynamicDescriptor.Get(), m_DynamicBuffer));

	// Live (skinned) dynamic positions exposed as SRV so the RT shading path can reconstruct Vertex::Position.
	device->writeDescriptorTable(
		sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_SRV(m_DynamicDescriptor.Get(), m_DynamicBuffer));

	// The BLAS reads the live dynamic positions (skinning output).
	BuildSkinned(bsDynamicTriShape, m_DynamicBuffer, static_cast<uint16_t>(sizeof(float4)), false);

	CreateMaterial();

	InitSkinToBones(bsDynamicTriShape);

	InitDismemberSkin(Util::Adapter::GetGeometryRuntimeData(bsDynamicTriShape).skinInstance);
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
