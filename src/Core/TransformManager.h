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

	// Writes both current and previous transforms (combined path)
	void WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform);

	void Flush(nvrhi::ICommandList* commandList);

	// Returns the single combined GPU buffer (TransformData format)
	nvrhi::IBuffer* GetBuffer() const { return m_Buffer; }
	nvrhi::IBuffer* GetMeshWorldBuffer() const { return m_MeshWorldBuffer; }
	uint32_t GetTransformCount() const { return static_cast<uint32_t>(m_TransformSlots.GetUsedByteSize() / sizeof(float3x4)); }

private:
	void CreateBuffer();

	// Two CPU mirrors for mesh world transforms
	ResourceSlotManager m_TransformSlots;       // slot = float3x4 (48 bytes)
	DirtyRangeTracker m_PrevTransformSlots;     // slot = float3x4 (48 bytes)

	// Single combined GPU buffer (TransformData = 2x float3x4 = 96 bytes per slot)
	nvrhi::BufferHandle m_Buffer;

	// Mesh world GPU buffer (TransformData = 2x float3x4 = 96 bytes per slot)
	nvrhi::BufferHandle m_MeshWorldBuffer;
};
