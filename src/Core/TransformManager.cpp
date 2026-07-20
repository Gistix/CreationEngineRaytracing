#include "Core/TransformManager.h"

#include "Renderer.h"

TransformManager::TransformManager()
	: m_Slots(sizeof(TransformData), Constants::NUM_MESHES_MAX, Constants::NUM_MESHES_MAX)
{
	CreateBuffer();
}

void TransformManager::CreateBuffer()
{
	auto device = Renderer::GetSingleton()->GetDevice();

	auto bufferDesc = nvrhi::BufferDesc()
		.setByteSize(m_Slots.GetCapacity())
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(false)
		.setDebugName("Transform Buffer");

	m_Buffer = device->createBuffer(bufferDesc);
}

uint32_t TransformManager::AllocateTransformIndex()
{
	uint64_t offset = m_Slots.Allocate();
	return m_Slots.GetIndexFromOffset(offset);
}

void TransformManager::ReleaseTransformIndex(uint32_t index)
{
	m_Slots.Release(index * sizeof(TransformData));
}

void TransformManager::WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform)
{
	TransformData data;
	data.Transform = transform;
	data.PrevTransform = prevTransform;
	m_Slots.Write(index * sizeof(TransformData), &data, sizeof(TransformData));
}

void TransformManager::Flush(nvrhi::ICommandList* commandList)
{
	auto dirtyRanges = m_Slots.ConsumeDirtyRanges();

	for (const auto& [offset, size] : dirtyRanges)
		commandList->writeBuffer(m_Buffer, static_cast<const uint8_t*>(m_Slots.GetMirror()) + offset, size, offset);
}
