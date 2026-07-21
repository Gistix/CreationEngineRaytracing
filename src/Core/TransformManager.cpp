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

	auto inputDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(float3x4))
		.setStructStride(sizeof(float3x4))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(false);

	m_CurrentBuffer = device->createBuffer(inputDesc.setDebugName("Current Transform Buffer"));
	m_PrevBuffer = device->createBuffer(inputDesc.setDebugName("Prev Transform Buffer"));

	auto outDesc = nvrhi::BufferDesc()
		.setByteSize(Constants::NUM_MESHES_MAX * sizeof(TransformData))
		.setStructStride(sizeof(TransformData))
		.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource)
		.setCanHaveUAVs(true)
		.setDebugName("Transform Buffer");

	m_Buffer = device->createBuffer(outDesc);
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
	{
		auto dirtyRanges = m_TransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_TransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_CurrentBuffer, mirror + offset, size, offset);
	}

	{
		auto dirtyRanges = m_PrevTransformSlots.ConsumeDirtyRanges();
		const uint8_t* mirror = static_cast<const uint8_t*>(m_PrevTransformSlots.GetMirror());
		for (const auto& [offset, size] : dirtyRanges)
			commandList->writeBuffer(m_PrevBuffer, mirror + offset, size, offset);
	}
}
