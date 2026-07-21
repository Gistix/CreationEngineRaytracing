#pragma once

#include "Core/DirtyRangeTracker.h"
#include "Core/ResourceSlotManager.h"
#include "Transform.hlsli"

class TransformManager
{
public:
	TransformManager();

	uint32_t AllocateTransformIndex();
	void ReleaseTransformIndex(uint32_t index);

	void WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform);

	void Flush(nvrhi::ICommandList* commandList);

	nvrhi::IBuffer* GetBuffer() const { return m_Buffer; }
	nvrhi::IBuffer* GetCurrentBuffer() const { return m_CurrentBuffer; }
	nvrhi::IBuffer* GetPrevBuffer() const { return m_PrevBuffer; }
	uint32_t GetTransformCount() const { return static_cast<uint32_t>(m_TransformSlots.GetUsedByteSize() / sizeof(float3x4)); }

private:
	void CreateBuffer();

	ResourceSlotManager m_TransformSlots;    // slot = float3x4 (48 bytes)
	DirtyRangeTracker m_PrevTransformSlots;  // slot = float3x4 (48 bytes)

	nvrhi::BufferHandle m_Buffer;
	nvrhi::BufferHandle m_CurrentBuffer;
	nvrhi::BufferHandle m_PrevBuffer;
};
