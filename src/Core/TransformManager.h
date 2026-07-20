#pragma once

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
	uint32_t GetTransformCount() const { return static_cast<uint32_t>(m_Slots.GetUsedByteSize() / sizeof(TransformData)); }

	TransformData* GetMirror() { return reinterpret_cast<TransformData*>(m_Slots.GetMirror()); }

private:
	void CreateBuffer();

	ResourceSlotManager m_Slots;
	nvrhi::BufferHandle m_Buffer;
};
