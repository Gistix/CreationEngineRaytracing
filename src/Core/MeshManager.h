#pragma once

#include "Core/DirtyRangeTracker.h"
#include "Core/ResourceSlotManager.h"
#include "Mesh.hlsli"
#include "Properties.hlsli"
#include "Transform.hlsli"

#include <mutex>

class MeshManager
{
public:
	MeshManager();

	uint32_t AllocateMeshIndex();
	void ReleaseMeshIndex(uint32_t index);

	uint32_t AllocateGeometryIndex();
	void ReleaseGeometryIndex(uint32_t index);

	void WriteTransformData(uint32_t index, const float3x4& transform, const float3x4& prevTransform);
	void WriteMeshData(uint32_t index, const MeshData& meshData);
	void WritePropertiesData(uint32_t index, const PropertiesData& data);

	// Grass instance transforms. The pool is pre-sized (Constants::NUM_GRASS_INSTANCES_MAX) so
	// groups never invalidate existing BLAS geometry transforms.
	uint32_t AllocateGrassTransforms(uint32_t count);
	void ReleaseGrassTransforms(uint32_t base, uint32_t count);
	void WriteGrassTransform(uint32_t index, const float3x4& transform, const float3x4& prevTransform);

	void Flush(nvrhi::ICommandList* commandList);

	nvrhi::IBuffer* GetMeshBuffer() const { return m_MeshBuffer; }
	nvrhi::IBuffer* GetPropertiesBuffer() const { return m_PropertiesBuffer; }
	nvrhi::IBuffer* GetTransformBuffer() const { return m_Buffer; }
	nvrhi::IBuffer* GetCurrentTransformBuffer() const { return m_CurrentBuffer; }
	nvrhi::IBuffer* GetPrevTransformBuffer() const { return m_PrevBuffer; }
	nvrhi::IBuffer* GetGrassTransformBuffer() const { return m_GrassTransformBuffer; }

private:
	void CreateBuffers();

	// Mesh slot allocation (index = byte offset / 4)
	ResourceSlotManager m_MeshSlots;
	ResourceSlotManager m_GeometrySlots;

	DirtyRangeTracker m_TransformSlots;
	DirtyRangeTracker m_PrevTransformSlots;

	ResourceSlotManager m_PropertiesSlots;

	DirtyRangeTracker m_GrassTransformSlots;
	std::mutex m_GrassTransformMutex;
	struct GrassRange
	{
		uint32_t base;
		uint32_t count;
	};
	eastl::vector<GrassRange> m_FreeGrassRanges;
	uint32_t m_GrassTransformNext = 0;

	nvrhi::BufferHandle m_MeshBuffer;
	nvrhi::BufferHandle m_PropertiesBuffer;
	nvrhi::BufferHandle m_Buffer;
	nvrhi::BufferHandle m_CurrentBuffer;
	nvrhi::BufferHandle m_PrevBuffer;
	nvrhi::BufferHandle m_GrassTransformBuffer;
};
