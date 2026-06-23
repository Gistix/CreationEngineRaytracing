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

	auto device = Renderer::GetSingleton()->GetDevice();

	auto& runtimeData = bsDynamicTriShape->GetDynamicTrishapeRuntimeData();

	if (runtimeData.dataSize == 0) {
		logger::warn("DynamicMesh::DynamicMesh - No dynamic data for {}", m_Name);
		return;
	}

	// Allocate space for dynamic data
	m_DynamicData.resize(runtimeData.dataSize, 0u);

	runtimeData.lock.Lock();
	UpdateDynamicData(runtimeData.dynamicData, runtimeData.dataSize);
	runtimeData.lock.Unlock();

	// Dynamic positions are float4 per vertex; the BLAS reads them as RGB32_FLOAT with a float4
	// stride so the trailing w component is skipped.

	// Live (skinning output) positions; BLAS/RT source.
	auto liveBufferDesc = nvrhi::BufferDesc()
		.setByteSize(runtimeData.dataSize)
		.setStructStride(sizeof(float4))
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} - Dynamic (Live)", m_Name).c_str());

	m_DynamicBuffer = device->createBuffer(liveBufferDesc);

	// Original (rest/morph) positions copied from the game each frame; skinning input.
	auto originalBufferDesc = nvrhi::BufferDesc()
		.setByteSize(runtimeData.dataSize)
		.setStructStride(sizeof(float4))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setDebugName(std::format("{} - Dynamic (Original)", m_Name).c_str());

	m_OriginalDynamicBuffer = device->createBuffer(originalBufferDesc);

	// Seed the original buffer with the initial morph positions and copy them into the live buffer
	// so the first BLAS build has valid data before the skinning pass runs.
	commandList->writeBuffer(m_OriginalDynamicBuffer, m_DynamicData.data(), m_DynamicData.size());
	commandList->copyBuffer(m_DynamicBuffer, 0, m_OriginalDynamicBuffer, 0, runtimeData.dataSize);
	m_NeedsUpload = false;

	// Register at a shared slot: original -> SRV (input), live -> UAV (output).
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	m_DynamicDescriptor = sceneGraph->GetDynamicVertexDescriptors()->m_DescriptorTable->CreateDescriptorHandle(
		nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_OriginalDynamicBuffer));

	device->writeDescriptorTable(
		sceneGraph->GetDynamicVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_UAV(m_DynamicDescriptor.Get(), m_DynamicBuffer));

	BuildSkinned(bsDynamicTriShape, m_DynamicBuffer, static_cast<uint16_t>(sizeof(float4)), false);
}

void DynamicMesh::UpdateDynamicData(void* dynamicData, uint32_t dataSize)
{
	if (std::memcmp(m_DynamicData.data(), dynamicData, dataSize) == 0)
		return;

	std::memcpy(m_DynamicData.data(), dynamicData, dataSize);

	m_NeedsUpload = true;

	// Flag a vertex update so the cluster uploads the initial data before its first BLAS build.
	MarkDirty(DirtyFlags::Vertex);
}

void DynamicMesh::UploadBuffers(nvrhi::ICommandList* commandList)
{
	if (!m_NeedsUpload || !m_OriginalDynamicBuffer)
		return;

	// Upload the latest morph positions to the skinning input; the skinning pass produces the live buffer.
	commandList->writeBuffer(m_OriginalDynamicBuffer, m_DynamicData.data(), m_DynamicData.size());

	m_NeedsUpload = false;
}
