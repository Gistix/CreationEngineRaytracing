#include "Core/TransformManager.h"

#include "Renderer.h"

TransformManager::TransformManager()
	: m_TransformSlots(sizeof(float3x4), Constants::NUM_MESHES_MAX, Constants::NUM_MESHES_MAX)
	, m_PrevTransformSlots(sizeof(float3x4), Constants::NUM_MESHES_MAX)
{
	CreateBuffer();
}

void TransformManager::CreateBuffer()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	// Combined buffer: each slot holds a TransformData (Transform + PrevTransform = 2x float3x4)
	// UAV-capable for TransformComposition compute shader; also used as SRV for BLAS builds.
	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(TransformData))
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(true)
		.setDebugName("Transform Buffer");

	m_Buffer = device->createBuffer(bufferDesc);

	// Mesh world buffer: same layout as TransformData (current + prev = 2x float3x4)
	// SRV-only, read by TransformComposition compute shader.
	auto meshBufferDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(TransformData))
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(false)
		.setDebugName("Mesh World Buffer");

	m_MeshWorldBuffer = device->createBuffer(meshBufferDesc);
}

uint32_t TransformManager::AllocateTransformIndex()
{
	uint64_t offset = m_TransformSlots.Allocate();
	return m_TransformSlots.GetIndexFromOffset(offset);
}

void TransformManager::ReleaseTransformIndex(uint32_t index)
{
	m_TransformSlots.Release(index * sizeof(float3x4));
}

void TransformManager::WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	m_TransformSlots.Write(index * sizeof(float3x4), &transform, sizeof(float3x4));
	m_PrevTransformSlots.Write(index * sizeof(float3x4), &prevTransform, sizeof(float3x4));
}

void TransformManager::Flush(nvrhi::ICommandList* commandList)
{
	static constexpr uint32_t kTransformSize = sizeof(float3x4);
	static constexpr uint32_t kSlotSize = sizeof(TransformData); // 2 * float3x4

	// Flush mesh world transforms — map from 48-byte mirror to first half of each GPU slot
	{
		auto dirtyRanges = m_TransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_TransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges) {
			const uint64_t slotIndex = offset / kTransformSize;
			const uint64_t gpuOffset = slotIndex * kSlotSize;
			commandList->writeBuffer(m_MeshWorldBuffer, mirror + offset, size, gpuOffset);
		}
	}

	// Flush mesh previous world transforms — map from 48-byte mirror to second half of each GPU slot
	{
		auto dirtyRanges = m_PrevTransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_PrevTransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges) {
			const uint64_t slotIndex = offset / kTransformSize;
			const uint64_t gpuOffset = slotIndex * kSlotSize + kTransformSize;
			commandList->writeBuffer(m_MeshWorldBuffer, mirror + offset, size, gpuOffset);
		}
	}
}
