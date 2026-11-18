#include "Core/Mesh/TreeAnimMesh.h"
#include "Pass/Raytracing/Common/VertexAnimation.h"
#include "Renderer/RenderNode.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Util.h"
#include "Types/RE/RE.h"

TreeAnimMesh::TreeAnimMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
	: Mesh(bsTriShape, commandList)
{
	if (!IsReady())
		return;
	m_IsReady = false;

	m_Type = Type::TreeAnim;

	const auto slotIndex = m_VertexBuffer.m_Descriptor.Get();
	auto device = Renderer::GetSingleton()->GetDevice();
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	const auto& triShapeData = Util::Adapter::GetTrishapeRuntimeData(bsTriShape);
	const auto vertexStride = Util::Geometry::GetStoredVertexSize(m_VertexDesc);
	const auto byteSize = static_cast<size_t>(triShapeData.vertexCount) * vertexStride;

	m_VertexCount = triShapeData.vertexCount;

	// Live (output) buffer: raw-viewable + UAV + BLAS input.
	auto liveBufDesc = nvrhi::BufferDesc()
		.setByteSize(byteSize)
		.setCanHaveRawViews(true)
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} (Tree Anim Live Vertex Buffer)", m_Name.c_str()));

	m_LiveVertexBuffer = device->createBuffer(liveBufDesc);
	if (!m_LiveVertexBuffer)
		return;

	// Seed the live buffer with the rest pose (carries UV/normal/color the animation never writes).
	// Prefer the CPU copy so the shared native buffer is not barriered; fall back to a GPU copy.
	auto* rendererData = Util::Adapter::GetGeometryRuntimeData(bsTriShape).rendererData;
	if (const auto* vertexData = rendererData ? Util::Adapter::GetVertexData(rendererData) : nullptr)
		commandList->writeBuffer(m_LiveVertexBuffer, vertexData, byteSize);
	else
		commandList->copyBuffer(m_LiveVertexBuffer, 0, m_VertexBuffer.m_Buffer, m_VertexBuffer.m_Offset, byteSize);

	commandList->setBufferState(m_LiveVertexBuffer,
		nvrhi::ResourceStates::ShaderResource | nvrhi::ResourceStates::AccelStructBuildInput);
	commandList->commitBarriers();

	// Prev-position buffer (float3 per vertex) for per-vertex motion vectors.
	auto prevPositionBufferDesc = nvrhi::BufferDesc()
		.setByteSize(sizeof(float3) * m_VertexCount)
		.setStructStride(sizeof(float3))
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setDebugName(std::format("{} (Tree Anim Prev Position Buffer)", m_Name.c_str()));

	m_PrevPositionBuffer = device->createBuffer(prevPositionBufferDesc);
	if (!m_PrevPositionBuffer)
		return;

	// Repoint the geometry desc to the live buffer for BLAS reads.
	for (auto& entry : m_GeometryEntries) {
		entry.desc.geometryData.triangles.vertexBuffer = m_LiveVertexBuffer;
		entry.desc.geometryData.triangles.vertexOffset = 0;
	}

	// RT shaders read the live buffer.
	device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
		nvrhi::BindingSetItem::RawBuffer_SRV(slotIndex, m_LiveVertexBuffer));

	// VertexAnimation reads the original (native) buffer.
	device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_SRV(slotIndex, m_VertexBuffer.m_Buffer, nvrhi::BufferRange(m_VertexBuffer.m_Offset, byteSize)));

	// VertexAnimation writes the live buffer.
	device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_UAV(slotIndex, m_LiveVertexBuffer));

	// Prev positions: SRV (RT read) + UAV (VertexAnimation write).
	device->writeDescriptorTable(sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_SRV(slotIndex, m_PrevPositionBuffer));
	device->writeDescriptorTable(sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_UAV(slotIndex, m_PrevPositionBuffer));

	logger::debug("TreeAnimMesh - {} promoted to static tree animation ({} vertices)", m_Name.c_str(), m_VertexCount);
	m_IsReady = true;
}

void TreeAnimMesh::Update(nvrhi::ICommandList* commandList)
{
	Mesh::Update(commandList);

	auto* animationPass = Renderer::GetSingleton()->GetRenderGraph()->GetPass<Pass::VertexAnimation>();
	if (!animationPass)
		return;

	VertexAnimationData data{};
	if (!m_TreeAnim.Update(Util::Adapter::GetGeometryRuntimeData(m_BSTriShape).shaderProperty, data.Params))
		return;

	data.Index = GetVertexID();
	data.VertexCount = m_VertexCount;
	data.Flags = VertexAnimation::Flags::TreeAnim;

	const uint64_t vertexDescRaw = GetVertexDescRaw();
	std::memcpy(&data.VertexDesc, &vertexDescRaw, sizeof(uint64_t));

	animationPass->QueueUpdate(this, data);

	MarkDirty(DirtyFlags::Vertex);
}

void TreeAnimMesh::CollectOwnedBuffers(OwnedBufferStats& stats) const
{
	stats.treeAnimMeshCount++;
	if (m_LiveVertexBuffer)
		stats.treeAnimLiveVertexBytes += m_LiveVertexBuffer->getDesc().byteSize;
	if (m_PrevPositionBuffer)
		stats.treeAnimPrevPositionBytes += m_PrevPositionBuffer->getDesc().byteSize;
}
