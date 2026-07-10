#include "Core/SkinnedMesh.h"
#include "Renderer.h"
#include "Util.h"
#include "Constants.h"
#include "Scene.h"
#include "SceneGraph.h"
#include "Utils/Adapter.h"
#include "Renderer/RenderNode.h"
#include "Pass/Raytracing/Common/Skinning.h"
#include "Types/RE/RE.h"

static void PackNiTransform(const RE::NiTransform& src, NiTransformPacked& dst)
{
	dst.Rot0_Scale = float4(src.rotate.entry[0][0], src.rotate.entry[0][1], src.rotate.entry[0][2], src.scale);
	dst.Rot1       = float4(src.rotate.entry[1][0], src.rotate.entry[1][1], src.rotate.entry[1][2], 0.0f);
	dst.Rot2       = float4(src.rotate.entry[2][0], src.rotate.entry[2][1], src.rotate.entry[2][2], 0.0f);
	dst.Translate  = float4(src.translate.x, src.translate.y, src.translate.z, 0.0f);
}

static void PackNiTransform(const RE::NiTransform& src, float4& r0s, float4& r1, float4& r2, float4& t)
{
	r0s = float4(src.rotate.entry[0][0], src.rotate.entry[0][1], src.rotate.entry[0][2], src.scale);
	r1  = float4(src.rotate.entry[1][0], src.rotate.entry[1][1], src.rotate.entry[1][2], 0.0f);
	r2  = float4(src.rotate.entry[2][0], src.rotate.entry[2][1], src.rotate.entry[2][2], 0.0f);
	t   = float4(src.translate.x, src.translate.y, src.translate.z, 0.0f);
}

SkinnedMesh::SkinnedMesh(RE::BSTriShape* bsTriShape, nvrhi::ICommandList* commandList)
{
	m_Name = MakeDebugName(bsTriShape);
	m_BSTriShape = bsTriShape;
	m_Type = Type::Skinned;

	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	auto* skinInstance = geometryData.skinInstance.get();
	if (!skinInstance) {
		logger::warn("SkinnedMesh::SkinnedMesh - No skin instance for {}", m_Name);
		return;
	}

	const auto& skinPartition = skinInstance->skinPartition;
	if (!skinPartition || skinPartition->numPartitions == 0) {
		logger::warn("SkinnedMesh::SkinnedMesh - No skin partitions for {}", m_Name);
		return;
	}

	auto* basePartitionBuffer = skinPartition->partitions[0].buffData;
	if (!basePartitionBuffer) {
		logger::warn("SkinnedMesh::SkinnedMesh - No base partition buffer for {}", m_Name);
		return;
	}

	const uint32_t vertexCount = skinPartition->vertexCount;

	if (!ValidateCounts(skinPartition->partitions[0].triangles, vertexCount))
		return;

	// All partitions share a single vertex buffer; create it once from the first partition.
	// This native buffer is the original (rest-pose) source consumed by the skinning pass.
	m_VertexBuffer = CreateVertexBuffer(basePartitionBuffer);
	if (!m_VertexBuffer.m_Buffer)
		return;

	m_VertexCount = vertexCount;

	const uint16_t vertexStride = Util::Geometry::GetStoredVertexSize(basePartitionBuffer->vertexDesc);

	// Create the live (output) buffer + prev positions and register everything at the shared slot.
	// The BLAS reads the live buffer (skinning output), not the native original.
	CreateSkinningBuffers(commandList, basePartitionBuffer, vertexCount, vertexStride);

	BuildSkinned(bsTriShape, m_LiveVertexBuffer, vertexStride, true);

	CreateMaterial();

	InitSkinToBones(skinInstance);

	InitDismemberSkin(skinInstance);
}

void SkinnedMesh::UpdateLocalTransform(const float4x4& invTransform, const float4x4& prevInvTransform)
{
	BaseMesh::UpdateLocalTransform(invTransform, prevInvTransform);

	if (m_Flags.all(Flags::DismemberSkinInstance)) {
		for (auto& desc : m_VisibleGeometryDescs)
			desc.setTransform(m_LocalTransform.f);
	}
}

void SkinnedMesh::InitSkinToBones(RE::NiSkinInstance* skinInstance)
{
	auto* skinData = skinInstance->skinData.get();
	if (!skinData || skinData->bones == 0)
		return;

	m_SkinToBones.resize(skinData->bones);
	for (uint32_t i = 0; i < skinData->bones; i++)
		PackNiTransform(skinData->boneData[i].skinToBone, m_SkinToBones[i]);
}

void SkinnedMesh::InitDismemberSkin(RE::NiSkinInstance* skinInstance)
{
	const bool isDismemberSkinInstance = netimmerse_cast<RE::BSDismemberSkinInstance*>(skinInstance) != nullptr;
	if (!isDismemberSkinInstance)
		return;

	m_Flags.set(Flags::DismemberSkinInstance);
	Util::Geometry::GetDismemberPartitionVisibility(skinInstance, m_PartitionVisibility);
	RefreshVisibleGeometryCache();
}

void SkinnedMesh::CreateSkinningBuffers(nvrhi::ICommandList* commandList, RE::BSGraphics::TriShape* sourceTriShape, uint32_t vertexCount, uint16_t vertexStride)
{
	auto device = Renderer::GetSingleton()->GetDevice();
	auto* sceneGraph = Scene::GetSingleton()->GetSceneGraph();

	const uint32_t slot = m_VertexBuffer.m_Descriptor.Get();
	size_t vertexBufferSize = m_VertexBuffer.m_Buffer->getDesc().byteSize;

	// Model space normal maps require that we store the skinning TBN so they are transformed properly into world space
	if (m_BSTriShape->GetGeometryRuntimeData().shaderProperty->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kModelSpaceNormals)) {
		m_ModelSpaceNormal = true;

		// Rotation stored as a quaternion
		vertexBufferSize += 8ull * vertexCount;	
	}

	// Live (output) buffer: device-owned, raw-viewable + UAV + BLAS input.
	auto liveBufferDesc = nvrhi::BufferDesc()
		.setByteSize(vertexBufferSize)
		.setCanHaveRawViews(true)
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setIsAccelStructBuildInput(true)
		.setDebugName(std::format("{} (Live Vertex Buffer)", m_Name.c_str()).c_str());

	m_LiveVertexBuffer = device->createBuffer(liveBufferDesc);

	// Seed the live buffer from the CPU rest-pose data (carries UV/color/etc. that skinning never writes).
	// Avoids barriering the shared native source buffer, which several meshes wrap independently.
	const size_t seedSize = static_cast<size_t>(vertexCount) * vertexStride;
	commandList->writeBuffer(m_LiveVertexBuffer, Util::Adapter::GetVertexData(sourceTriShape), seedSize);

	// Prev-position buffer (float3 per vertex) for per-vertex motion vectors.
	auto prevPositionBufferDesc = nvrhi::BufferDesc()
		.setByteSize(sizeof(float3) * vertexCount)
		.setStructStride(sizeof(float3))
		.setCanHaveUAVs(true)
		.enableAutomaticStateTracking(nvrhi::ResourceStates::NonPixelShaderResource)
		.setDebugName(std::format("{} (Prev Position Buffer)", m_Name.c_str()).c_str());

	m_PrevPositionBuffer = device->createBuffer(prevPositionBufferDesc);

	// RT reads the live buffer (repoint the slot previously set to the native buffer).
	device->writeDescriptorTable(sceneGraph->GetVertexDescriptors()->m_DescriptorTable->GetDescriptorTable(),
		nvrhi::BindingSetItem::RawBuffer_SRV(slot, m_LiveVertexBuffer));

	// Skinning reads the original (native) buffer.
	device->writeDescriptorTable(sceneGraph->GetVertexCopyDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_SRV(slot, m_VertexBuffer.m_Buffer));

	// Skinning writes the live buffer.
	device->writeDescriptorTable(sceneGraph->GetVertexWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::RawBuffer_UAV(slot, m_LiveVertexBuffer));

	// Prev positions: SRV (RT read) + UAV (skinning write).
	device->writeDescriptorTable(sceneGraph->GetPrevPositionDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_SRV(slot, m_PrevPositionBuffer));
	device->writeDescriptorTable(sceneGraph->GetPrevPositionWriteDescriptors()->m_DescriptorTable,
		nvrhi::BindingSetItem::StructuredBuffer_UAV(slot, m_PrevPositionBuffer));
}

void SkinnedMesh::BuildSkinned(RE::BSTriShape* bsTriShape, nvrhi::IBuffer* vertexBuffer, uint16_t vertexStride, bool requireSharedNativeVertexBuffer)
{
	const auto& geometryData = bsTriShape->GetGeometryRuntimeData();

	auto* skinInstance = geometryData.skinInstance.get();
	if (!skinInstance)
		return;

	const auto& skinPartition = skinInstance->skinPartition;
	if (!skinPartition || skinPartition->numPartitions == 0)
		return;

	auto* basePartitionBuffer = skinPartition->partitions[0].buffData;

	const uint32_t vertexCount = skinPartition->vertexCount;

	std::memcpy(&m_VertexDesc, &basePartitionBuffer->vertexDesc, sizeof(m_VertexDesc));

	m_IndexBuffers.reserve(skinPartition->numPartitions);
	m_GeometryDescs.reserve(skinPartition->numPartitions);
	m_GeometryPartitionIndices.reserve(skinPartition->numPartitions);

	for (size_t i = 0; i < skinPartition->numPartitions; i++)
	{
		const auto& partition = skinPartition->partitions[i];

		auto* partitionBuffer = partition.buffData;
		if (!partitionBuffer) {
			logger::warn("SkinnedMesh::BuildSkinned - Partition {} has no buffer for {}, skipping partition.", i, m_Name);
			continue;
		}

		if (partition.triangles == 0) {
			logger::warn("SkinnedMesh::BuildSkinned - Partition {} has no triangles for {}, skipping partition.", i, m_Name);
			continue;
		}

		// Enforce the single-vertex-buffer invariant: every partition must reference the same vertex buffer.
		if (requireSharedNativeVertexBuffer && partitionBuffer->vertexBuffer != basePartitionBuffer->vertexBuffer) {
			logger::warn("SkinnedMesh::BuildSkinned - Partition {} vertex buffer differs from partition 0 for {}, skipping mesh.", i, m_Name);
			m_IndexBuffers.clear();
			m_GeometryDescs.clear();
			m_VertexBuffer = {};
			return;
		}

		auto indexBuffer = CreateIndexBuffer(partitionBuffer);
		if (!indexBuffer.m_Buffer) {
			logger::warn("SkinnedMesh::BuildSkinned - Failed to create partition {} index buffer for {}, skipping partition.", i, m_Name);
			continue;
		}

		const uint32_t indexCount = static_cast<uint32_t>(partition.triangles) * 3;

		auto& emplacedIndexBuffer = m_IndexBuffers.emplace_back(std::move(indexBuffer));
		m_GeometryDescs.push_back(MakeGeometryDesc(emplacedIndexBuffer.m_Buffer, indexCount, vertexBuffer, vertexStride, vertexCount));
		m_GeometryPartitionIndices.push_back(i);
	}
}

void SkinnedMesh::Update(nvrhi::ICommandList* commandList)
{
	BaseMesh::Update(commandList);

	const auto& geometryData = m_BSTriShape->GetGeometryRuntimeData();

	if (auto* skinInstance = geometryData.skinInstance.get()) {
		auto* scene = Scene::GetSingleton();
		const bool isPathTracing = scene->IsPathTracingActive();
		const bool isForceCulled = isPathTracing; // Should also check for kEye and kEnvMap materials

		bool isVisible = false;
		if (isForceCulled)
			isVisible = scene->GetSceneGraph()->GetCamera()->NodeInFrustum(m_BSTriShape);

		// Only recompute when the game advanced the animation this frame.
		const auto frameID = skinInstance->frameID;

		if (isVisible || m_SkinFrameID != frameID) {
			m_SkinFrameID = frameID;

			auto* skinData = skinInstance->skinData.get();
			if (skinData && skinData->bones != 0) {
				if (m_BoneWorlds.size() != skinData->bones)
					m_BoneWorlds.resize(skinData->bones);

				for (uint32_t i = 0; i < skinData->bones; i++)
					PackNiTransform(*skinInstance->boneWorldTransforms[i], m_BoneWorlds[i]);

				PackNiTransform(m_BSTriShape->world.Invert(), m_GeomInv_Rot0_Scale, m_GeomInv_Rot1, m_GeomInv_Rot2, m_GeomInv_Translate);

				MarkDirty(DirtyFlags::Skin);
			}
		}

		// Dismember update.
		if (m_Flags.all(Flags::DismemberSkinInstance)) {
			const auto previousVisibility = m_PartitionVisibility;
			Util::Geometry::GetDismemberPartitionVisibility(skinInstance, m_PartitionVisibility);

			if (previousVisibility != m_PartitionVisibility)
				MarkDirty(DirtyFlags::Visibility);

			RefreshVisibleGeometryCache();
		}
	}

	// Queue this mesh for the GPU skinning pass when the pose advanced or its vertices changed.
	if (m_DirtyFlags.any(DirtyFlags::Vertex, DirtyFlags::Skin)) {
		if (auto* skinningPass = Renderer::GetSingleton()->GetRenderGraph()->GetRootNode()->GetPass<Pass::Skinning>())
			skinningPass->QueueUpdate(m_DirtyFlags.get(), this);
	}
}

void SkinnedMesh::RefreshVisibleGeometryCache()
{
	m_VisibleGeometryDescs.clear();
	m_VisibleGeometrySourceIndices.clear();

	m_VisibleGeometryDescs.reserve(m_GeometryDescs.size());
	m_VisibleGeometrySourceIndices.reserve(m_GeometryDescs.size());

	for (size_t i = 0; i < m_GeometryDescs.size(); ++i) {
		const auto partitionIndex = (i < m_GeometryPartitionIndices.size()) ? m_GeometryPartitionIndices[i] : i;
		if (partitionIndex >= m_PartitionVisibility.size() || m_PartitionVisibility[partitionIndex] == 0)
			continue;

		m_VisibleGeometryDescs.push_back(m_GeometryDescs[i]);
		m_VisibleGeometrySourceIndices.push_back(i);
	}
}
